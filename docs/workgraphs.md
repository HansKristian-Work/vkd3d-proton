# Workgraphs

## Emulating WorkGraphs

Even when we eventually have work graph support in Vulkan (aside from the AMDX experimental extension),
we will need a plain emulated path. This is important for ...

### Debugability

Workgraphs are a complete black box just like RT
with the only observable action being input nodes going in and UAV writes going out.
For debugging on our end (RenderDoc) and crash debug (breadcrumbs), this is completely unacceptable,
and it is critical to have a solid fallback to plain old vkCmdDispatchIndirect that can be stepped through
one-by-one.

Graphics nodes are currently out of scope since it does not seem to be released yet (TIER_1_1).
The current proposal also only supports mesh shaders, which is very compute-like to begin with.
Hopefully we will not design ourselves into a corner here.

While a plain indirect fallback is likely impossible due to "infinite" potential memory requirements
of workgraphs, we can hopefully support any reasonable content.
Comically large workgraph dispatches will not run acceptably fast on real hardware after all,
and would therefore not be shippable.
The only limiting factor is how much VRAM we're willing to give intermediate buffers,
and sacrificing on the order of 1 GB is likely acceptable if it means being able to emulate the feature
somewhat competently. 16 GB+ VRAM GPUs tend to have a lot of VRAM to spare anyway.
We could also tweak that requirement on a content-to-content basis if need be.

It should be possible to write a feedback atomic on failure so that vkd3d-proton can flag an error.
In theory, increasing the command ring dynamically (with sparse maybe?) is also an option.

## Implementation details

This is currently used as a draft space.
It will be in constant flux as the implementation comes together.

### Global memory ring

Rather than asking applications to allocate *massive* scratch buffers, we should just
have one global buffer per queue family (GRAPHICS and COMPUTE), which are allocated on demand,
and reused between every DispatchGraph.
To synchronize access to the global buffer, we can mark a command list as being work graph
sensitive and do a full serialization of queues in that family on submission.
This ensures no synchronization bugs, and should be fine.
It's unlikely that applications will rely on two separate async compute queues doing overlapping graph work in parallel.

### Double buffered

There are node inputs and node outputs.
If our ring is e.g. 1 GB, we can dedicate the lower 512 MB for inputs, and upper half to outputs.
Nodes are allowed to write to the inputs.

The idea is to form a graph of node executions, so that we can do:

- Depth 0: Run entry point nodes
  - Barrier
  - Post-process to set-up indirect compute dispatches, clear counters, etc
  - Barrier
- Depth 1: Run all nodes which are at depth 1
  - Setup for depth 2
- ... and so on. Maximum depth is 32.

Between every iteration we can flip the output and input.
This isn't too different from how you'd implement dependent GPU work today.
Having long-running, persistent workgroups that pull in work from a ring is not in-spec (forward progress guarantees),
and as a driver, we have no way to meaningfully load balance it, since we don't know the input workload.
It would also be very bad for debugability, since it's also a black box from an observability standpoint.

### Output data structures

#### Record counter

For each potential output node, have an SSBO of counters.

```
buffer OutputCountsInPass
{
    uint counts[];
};
```

When output records are allocated, do atomic increment at a particular offset.
Which offset to use depends on the NodeID. We could statically map this through spec constants, e.g.

```
layout(constant_id = 0) const uint OUTPUT_NODE_OFFSET = 0;
// ThreadNodeOutputRecords<recordType> 
//    NodeOutput<recordType>::GetThreadNodeOutputRecords(uint numRecordsForThisThread)
atomicAdd(counts[OUTPUT_NODE_OFFSET], numRecordsForThisThread);
```

For NodeOutputArray, we can offset the index into counts easily.
Alternatively, it's possible to feed the offset through a UBO,
which may become meaningful for COLLECTION linking,
but we'll ignore COLLECTIONs and AddToStateObject (ugggggh) for now.

#### Payload allocation

A flat buffer that holds the majority of data.
Based on number of records, we can allocate `alignUp(numRecords * sizeof(Payload))`.
This offset into payload buffer can be stored in a buffer.

```
layout(constant_id = 0) const uint OUTPUT_NODE_OFFSET = 0;

uniform Constants
{
    uvec2 payloadBDA;
    uint maxNumRecordsPerNode;
};

buffer PayloadOffsets
{
    uint payloadOffsets[];
};

uint recordOffset = atomicAdd(counts[OUTPUT_NODE_OFFSET], numRecords) + OUTPUT_NODE_OFFSET * maxNumRecordsPerNode;
uint payloadOffset = atomicAdd(payloadRing, align(sizeof(payload) * numRecords);

// Loop over all records. MaxRecords can only be up to 256, and this is per thread-group.
payloadOffsets[recordOffset + i] = payloadOffset + i * sizeof(payload);

// GetNodePointer()
PayloadPointerType ptr = PayloadPointerType(payloadBDA + payloadOffset);
```

It's also possible we could be smarter and write payloadOffset as a packed u32.
The upper 24 bits could be `offset / alignment`, and lower 8 bits holds count minus 1.
However, this is likely more complicated than it needs to be, but it may be necessary to keep memory usage down.
In a scenario where a thread node emits 256 nodes in one go, we would get a tremendous waste of memory.
A work-stealing algorithm should be able to work with the more packed representation.
Another possibility is that the packed representation can be expanded into the more linear representation
in a separate compute pass. Balancing complexity in dxil-spirv and meta shaders will likely be important here ...

#### RW tracking

Node inputs to broadcast nodes can be `RW` tracking. The spec explicitly outlines that implementations should
reserve a uint in the payload struct to hold the necessary count.
On `OutputComplete()` for such a struct, we can read the emitted grid size, compute total thread groups and write that.
This is fine since `OutputComplete()` must be called in uniform control flow.
It is also possible to do this computation in a meta shader.
In that case, it needs to know the format (u16 vs u32) and dimension, and offset within the dispatch struct.

`FinishedCrossGroupSharing()` can be implemented with a simple atomic decrement.
The intent of this API is to support single-pass down-sampling or similar things.
This call must be dispatch grid uniform (is that stronger than thread group uniform? :v).
This requirement is useful since we don't have to auto-insert
`FinishedCrossGroupSharing()` at the end of the shader if the threadgroup didn't happen to call it.

#### IsValid

It should be possible pass spec constants and form a spec constant LUT, which can be looked up. E.g.

```
layout(constant_id = 0) const bool NODE_0_VALID = false;
layout(constant_id = 1) const bool NODE_1_VALID = false;
layout(constant_id = 2) const bool NODE_2_VALID = false;
layout(constant_id = 3) const bool NODE_3_VALID = false;
const bool IS_VALID_LUT[] = bool[](NODE_0_VALID, NODE_1_VALID, ...);
```

This can handle sparse node arrays (some nodes may not be valid).
We also have to deal with range checking, so that querying unbounded node arrays work, but all
of this can be handled with spec constants. If we have to consider COLLECTIONs later, we might have to
make this query dynamic using push UBOs.

#### Recursion

Recursion will not be particularly difficult. The same compute shader will just be invoked at multiple levels.
`IsValid()` and `GetRemainingRecursionLevels()` can both be implemented by passing down a constant.

#### Local root signatures

Just like DXR, a node can have a local root signature assigned to it.
Each node has a local root signature index assigned to it.
On the API side we bind a strided buffer.
The node at local index `L` will view local root signature data at address:

`ShaderRecordAddr = StartAddress + StrideInBytes * L`

This should be straight forward to implement.
`L` is uniform for a given dispatch,
so we can just bind the local root signature as a push SSBO.
We just need VA look-up to translate VA to VkBuffer + offset as usual.
dxil-spirv needs to have an option to replace `layout(shaderRecordEXT)` as plain SSBO with a provided set/binding.

#### Binding model

Global root signature parameters need to go in push constants as usual.
We will likely have to block uses of push UBOs.
Using pipeline layout compatibility like with DXR, we can append internal descriptor set layouts as appropriate.

- One immutable sampler set for local root signature static samplers.
- One push buffer set to pass down various meta-data needed to execute a node.

We have to ban user push UBOs here since it's not legal to have multiple push descriptor sets.
This shouldn't be a big problem. Just like DGC we require the appropriate vkd3d-config flag on NVIDIA.
The push UBO path will eventually have to go on NVIDIA anyway ...

Like DXR, we may have to insert `nonuniformEXT` in more places than we expect, especially for thread node.

### Executing nodes

When emitting code in dxil-spirv, we cannot avoid the need to generate a "wrapper" of some sort,
which receives work and sets up shader inputs.

```
PayloadPtr ptr;
void real_node_main() { ... }

void main() { convert_dispatch_grid(); ptr = set_input_payload(); real_node_main(); }
```

Using two indirect dispatches, we can always emit an arbitrary number of work groups.
This is slightly tricky, because the maximum group count dimension is `2^16 - 1`,
so unfortunately, we cannot rely on simple 1D workgroup dispatch.
To emit N workgroups, we can dispatch twice:

```
(2^15, N / 2^15, 1)
(N % 2^15, 1, 1)
```

In the second dispatch, we can read `N` to deduce the base offset.
Alternatively, one dispatch can loop to cover large N, but that adds more complexity to dxil-spirv codegen.

The shaders can then translate linear work group ID into the appropriate data needed for the node.
The implementation detail of these changes depending on the situation.

#### Thread

We will pick an appropriate thread group size, which should be equal to maximum subgroup size.
Once we have the total node count `T`, `N = alignDiv(T, SubgroupSize)`, and the shader can handle edge case of unaligned
size in last thread group trivially.

#### Coalesced

Very similar to thread model. We just have to divide with MaxCoalesceSize as defined by shader instead of
subgroup sizes. Instead of masking execution ourselves,
we will pass the count to shader instead and let it deal with it.

#### Broadcast

This one is more tricky, since grid size implies additional amplification on top of the other amplification.
E.g. a thread can generate 5 node outputs, which are then written with a grid size of (8, 7, 3).
It would be extremely impractical to unroll that level of amplification, so instead the idea is:

##### `[NodeDispatchGrid]`

This is static amplification. For data locality concerns, we can encode the indirect buffer as:

```
(NodeDispatchGrid.x * NodeDispatchGrid.y * NodeDispatchGrid.z, 2^15, N / 2^15)
(NodeDispatchGrid.x * NodeDispatchGrid.y * NodeDispatchGrid.z, N % 2^15, 1)
```

This assumes that the NodeDispatchGrid cannot exceed `2^16 - 1`, and I seriously doubt it can exceed that.
We can convert WorkGroupID.x to 3D dispatch grid with constant dividers in the shader, which should be fine.
Usually, these will be PoT anyway, and divide by constant is easy to optimize.

##### `[NodeMaxDispatchGrid]`

This is more tricky. We have to check against the dispatch grid in the shader before we call the real main function.
If MaxGrid is large, but app only dispatches a small number, we are somewhat hosed perf-wise as a lot of dummy threads
will be spawned that eat up occupancy.
[NVIDIA](https://developer.nvidia.com/blog/work-graphs-in-direct3d-12-a-case-study-of-deferred-shading/) has
recommendations to keep `NodeMaxDispatchGrid` as tight as possible, so it clearly affects their implementation as well.
If we have to cater to arbitrarily large max grid, we can consider using Z-order encoding of the dispatch grid size,
and only execute that many groups in X to minimize the bloat. This becomes very convoluted, very quickly.

## API side

One of the biggest pain points will be dealing with the extremely awkward subobject-based API.
It took the worsts parts of DXR and made it even worse. :(

A key part of this API is that all metadata from the DXIL shader can be overridden using subobjects
that override other subobjects.
We'll need an API in dxil-spirv to reflect all metadata associated with an entry point.
This isn't too far away from DXR.
On compilation we'll need an API to pass down metadata to dxil-spirv.
Compilation should ignore any metadata set in the DXIL itself,
but for a first iteration, we can just read shader metadata as-is.

### CPU node input

For CPU inputs, we have full knowledge up-front about how many nodes are being executed.
The main concern here is that we need to step through the buffer, `vkCmdUpdateBuffer` as appropriate,
and set up the input data structures, so we can start the dispatch.

From a memory management PoV it's possible to be somewhat robust against overflows.
Based on max node output limits we can statically determine maximum theoretical limits for how much
memory would be needed to execute a node input. If a single node input can fit in memory,
we know for sure that execution can succeed if we're willing to accept horrible performance and
split `DispatchGraph()` into N iterations.
`MULTI_CPU_INPUT` is similar here, it's just a more elaborate way of expressing it.

### GPU node input

```
typedef struct D3D12_NODE_GPU_INPUT
{
    UINT EntrypointIndex;
    UINT NumRecords;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE Records;
} D3D12_NODE_GPU_INPUT;
```

In this scenario we can dynamically select the EntrypointIndex, but all payload data is linear in memory.

Entry point nodes will likely need to be compilable in a special mode where it can
take raw BDA + stride as payload input instead of looking up payload offsets.

NumRecords can be transformed into 2x indirect dispatches.
NV_dgcc with compute pipeline switch can be handy here, since we can indirectly dispatch based on `EntrypointIndex`.
Without NV_dgcc pipeline switch, we will have to stamp out `NumEntryPoints` indirect dispatches and
only uses non-zero dispatch for the entry point we want. This is quite inefficient, but if number of entry points is
small, this shouldn't be a significant issue.

### Multi-GPU node input

```
typedef struct D3D12_MULTI_NODE_GPU_INPUT
{
    UINT NumNodeInputs;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE NodeInputs; // D3D12_GPU_NODE_INPUT array
} D3D12_MULTI_NODE_GPU_INPUT;
```

This is where true hell begins because unlike MDI, there is no MaxCommands here. It's impossible to determine
at CPU time how much memory may be consumed. We will need to dispatch a compute shader that parses the GPU node
data structures and instead of allocating payload memory, we just write the pointer to the payload memory instead.

It's plausible that NV_dgcc will come in clutch here.
We won't know NumNodeInputs, but if we assume some reasonable upper limit, we could generate an indirect command stream.

`NumNodeInputs` becomes the indirect count. It will be clamped to our maximum assumption (say e.g. 4k).

We can dispatch 4096 compute threads. They can process and generate 2 indirect dispatches.

```
NodeInputs[i].EntryPoint -> convert to 64-bit compute VA (PIPELINE token) through lookup
NodeInputs[i].Records -> convert to push constant update
NodeInputs[i].NumRecords -> convert to 3D dispatch grid (DISPATCH token)
```

This way it should be possible to drive the first level of the graph.
A non-DGC fallback would be even more horrid to implement :(

It seems like we cannot avoid updating push constants like this,
so we will likely have to augment our binding model to support root parameters as BDA, perhaps something like

```
layout(push_constant) uniform Registers
{
  RootParameterBDA rootParameters;
  PayloadBDA payloadStart;
  uint payloadStride;
};
```

Alternatively, we may need to flip everything on its head and move global root signature to push UBO,
and drive all metadata through push constants instead. ... So many things to consider with this dreaded feature :'(