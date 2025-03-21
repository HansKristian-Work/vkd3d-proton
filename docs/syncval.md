# Synchronization validation

`VKD3D_CONFIG=instruction_qa_checks` can do some detailed buffer sync validation now.
This document outlines how the implementation works, and where it falls short.
Full, absolute sync validation is borderline impossible for any practical game scenario.

The implementation extends the expect-assume code to support an ancillary buffer block that lives past
the normal control block of 1 KiB.

To enable `VKD3D_QA_HASHES=/tmp/hashes.txt` is used which include the shader hashes in question:

```
0000000000000000-ffffffffffffffff sync
```

Enables buffer sync validation for every single shader, which can be a problem. For convenience there is
also a way to only enable this validation for compute, which is where these problems typically arise.

```
0000000000000000-ffffffffffffffff sync-compute
```

In expect-assume, there is normally a 1 KiB "control block", and a larger "payload buffer"
which encodes information about a failure.
There is a trailing 8 bytes used to encode a "dispatch ID" and an atomic counter reserved for
getting unique IDs per invocation.

```
void AssumeTrue(bool value, uint inst)
{
    if ((!value) && ShouldReportInstrumentation)
    {
        // Load the dispatch ID, stores at offset 0x400 (the buffer is 0x408 bytes by default).
        uint _1863 = uint(InstrumentationControlData.atomics.length()) - 2u;
        uint _1866 = InstrumentationControlData.atomics[_1863];

        // Hash the failure so we ideally report every unique failure.
        uint _1872 = (((inst * 97u) ^ 2536414114u) ^ (_1866 * 51329u)) & (uint(InstrumentationData.data.length()) - 1u);

        // Try to allocate a failure in the control block.
        uint _1873 = _1872 >> 4u;
        uint _1875 = 1u << (_1872 & 15u);
        uint _1877 = atomicOr(InstrumentationControlData.atomics[_1873], _1875);
        ShouldReportInstrumentation = false;

        // If this is a new failure, report hash, instruction counter and the dispatch ID.
        if ((_1877 & _1875) == 0u)
        {
            InstrumentationData.data[_1872] = uvec4(2523780806u, 21187940u, inst, _1866);
            memoryBarrierBuffer();

            // Now mark the memory as valid. This ensures that if CPU reads "valid memory",
            // the payload data will be valid.
            // The CPU and GPU run somewhat out of sync, so we need to be careful about ordering,
            // assuming that memory ordering works correctly over the PCI-e bus of course ...
            uint _1885 = atomicOr(InstrumentationControlData.atomics[_1873], _1875 << 16u);
            memoryBarrierBuffer();
        }
    }
}
```

dxil-spirv inserts this call as necessary in response to `OpAssumeTrueKHR`.
At some point it may be possible to have GPU drivers insert a trap on failure instead.

On the CPU side, we peek at the 1 KiB ReBAR buffer, and report the failures as they come in.

For BDA validation, we augment the control block with an arbitrarily large block of memory, e.g. 128 MiB.
A larger buffer means lower chance of false positives, but slower runtime.
Even on a powerful GPU, full validation is likely to run at "barely interactive" frame rates ...

## Implementation

### Allocating a unique ID per thread

In the entry point, this is called so that we can allocate a unique (and vaguely hashed) ID.
This will be used for buffer validation since we need to way to ensure that buffer hazards
within the same invocation is not considered a real hazard. Otherwise, this code would fail:

```
ssbo.data[threadID] += 10;
```

```
uint AllocateInvocationID()
{
    uint _40 = atomicAdd(BloomBufferInvocation.atomics[uint(BloomBufferInvocation.atomics.length()) - 1u], 1103633207u);
    return _40;
}

uint InvocationID;

void main()
{
    InvocationID = AllocateInvocationID();
    // ... user code
}
```

### Bloom Filter

The core algorithm is the [Bloom Filter](https://en.wikipedia.org/wiki/Bloom_filter).
The idea is that given a GPU VA, we can insert that VA into the set, and then query if the VA exists later.
If it exists in the set, and InvocationID does *not* exist in that set, we have a hazard.
Of course, we need to consider different kinds of hazards. Some VA conflicts do not constitute a failure, e.g.:

- Load and Indirect command reads on the same VA is OK
- Different threads using atomics on the same VA is OK

We have 4 different contexts the algorithm can track:

- Non-atomic load
- Non-atomic store
- Atomic operation
- Indirect command read
  (not currently implemented in vkd3d-proton, since it requires a meta shader to inject instrumentation code)

### Hashing function

A Bloom Filter requires N unique hashing functions. We need to generate multiple keys.

```
uint AddrHash(uvec2 addr, uint prime)
{
    uvec2 _84 = uvec2(addr.x >> 4u, addr.y & 65535u);
    uvec2 _85 = uvec2(prime);
    uvec2 _92 = ((_84 >> uvec2(8u)) ^ _84.yx) * _85;
    uvec2 _96 = ((_92 >> uvec2(8u)) ^ _92.yx) * _85;
    uvec2 _100 = ((_96 >> uvec2(8u)) ^ _96.yx) * _85;
    uvec2 _104 = ((_100 >> uvec2(8u)) ^ _100.yx) * _85;
    uvec2 _108 = ((_104 >> uvec2(8u)) ^ _104.yx) * _85;
    return (((_108 >> uvec2(8u)) ^ _108.yx) * _85).x;
}
```

Then we just feed it N different primes, and hopefully it just works *shrug*.
The divide by 16 here is because we want to handle vectorized load-store in an efficient way.
Every bucket entry is a `uint64_t` where we use 64-bit atomics to encode hazards for the given 16 byte region.

### u64 payload layout

- Bits 0:15: Encodes if byte N within the 16 byte block is invalid to read.
- Bits 16:31: Encodes if byte N within the 16 byte block is invalid to write.
- Bits 32:35: Encodes if word N within the 16 byte block is invalid for use with atomics.
- Bits 36:39: Encodes if word N within the 16 byte block is invalid for use with indirect read.
- Bits 40:63: Reserved for InvocationID locking, which is explained later.

We only need 4 bits to encode atomics and indirect reads since they have at least 4 byte alignment.

NOTE: We cannot handle writes and loads that straddle a 16 byte boundary.
However, this is actually a blessing in disguise, since it lets us handle robustness rules for ByteAddressBuffer
exactly :) There is only a theoretical chance of a false negative since we are missing memory access into the adjacent block.

### Detecting a hazard

First, we compute various masks:

```
uint byte_mask = (bitfieldExtract(4294967295u, int(0u), int(len)) << (addr_lo & 15u)) & 65535u;
uint word_mask = (bitfieldExtract(4294967295u, int(0u), int((((addr_lo & 3u) + len) + 3u) >> 2u)) << bitfieldExtract(addr_lo, int(2u), int(2u))) & 15u;
uint hash_mask = bitfieldExtract(4294967295u, int(0u), int(uint(findMSB(uint(BloomBuffer.atomics.length())))));
uint hash_offset = bitfieldExtract(uint(BloomBuffer.atomics.length()), int(0u), int(uint(findMSB(uint(BloomBuffer.atomics.length()))))) - 1u;
uint bloom_index = (AddrHash(addr, 1103515245u) & hash_mask) + hash_offset;
uint bloom_index_1 = (AddrHash(addr, 1103518333u) & hash_mask) + hash_offset;
// ... As many as needed to reduce false positive rate.
```

Then we compute our invalidation mask:

```
uint64_t invalidation_mask =
    u64vec4(68719411200ul, 1099511627775ul, 1035087118335ul, 68719411200ul)[type] &
    packUint2x32(uvec2(byte_mask | (byte_mask << 16u), word_mask | (word_mask << 4u)));
```

The idea here is that given a type of access we need to invalidate future accesses:

- LOAD -> invalidate STORE + ATOMIC RMW
- STORE -> invalidate everything
- RMW -> invalidate everything except for RMW
- INDIRECT READ -> same as LOAD

Now we perform N 64-bit atomics to mark the VA as invalid.
We get back masks from the atomic where we can figure out which bytes were already invalidated.
Since this is a Bloom Filter, all bucket entries must have a hazard marked for us to consider it a hazard.
Due to the hashing nature, there will be stray invalidations that share one of our buckets, but it's very unlikely
that *all* buckets are falsely tagged as hazardous. We can make this arbitrarily unlikely by doing more atomics,
at the cost of GPU runtime performance. Currently, dxil-spirv does 16 of these which is the upper bound of practicality.

```
uint64_t prev_hazard_partial = atomicOr(BloomBuffer.atomics[bloom_index], invalidation_mask);
uvec2 prev_hazard = unpackUint2x32(prev_hazard_partial & prev_hazard_partial_1 & ...);
uint prev_hazard_lo = prev_hazard.x;
uint prev_hazard_hi = prev_hazard.y;
```

Based on our access type we can check `prev_hazard_lo` and `prev_hazard_hi` to figure out if there are problems.
If we have a hazard, we may need to disregard the failure. This can happen if the same thread already accessed the memory location.
This is where the InvocationID locking mechanism comes into play.
We treat the top 24 bits of each bucket as a "mini-bloom filter" of sorts.

```
// If there are no STORE hazards, it means we're the first thread to claim these bytes.
// Ignore any subsequent hazard by adding ourself to the lock set.
bool has_exclusive_access = ((prev_hazard_lo >> 16u) & byte_mask) == 0u;

// Generate a sparse randomized bitmask.
uint lock_mask = ((256u << bitfieldExtract(invocation_id, 0, 3)) | (65536u << bitfieldExtract(invocation_id, 3, 3))) | (16777216u << bitfieldExtract(invocation_id, 6, 3));
// Do atomics against the top 24 bits.
uint prev_lock = atomicOr(BloomBuffer32.atomics[bloom_index].y, has_exclusive_access ? lock_mask : 0u);
uint lock_mask_1 = ((256u << bitfieldExtract(invocation_id, 9, 3)) | (65536u << bitfieldExtract(invocation_id, 12, 3))) | (16777216u << bitfieldExtract(invocation_id, 15, 3));
uint prev_lock_1 = atomicOr(BloomBuffer32.atomics[bloom_index_1].y, has_exclusive_access ? lock_mask_1 : 0u);
// ...
```

If we can observe that InvocationID exists in this lock set, we assume we have a false positive, and we ignore the hazard.
This is a source of false negatives, but given enough stimulus, this should be irrelevant.

## Getting BDA values from descriptors and dealing with robustness

For root descriptors, we have the BDA inline, so that's easy, but for descriptors, we need to be a bit more clever.
We reuse the RTAS descriptor "heap", which is a buffer full of BDA pointers anyway.
When creating buffer descriptors, we encode the lower 48 bits of the BDA there, and for texel buffers, the element stride
is encoded in the top 16 bits. Other metadata could be placed there as necessary, but that's unused for now.

The shader loads this BDA value in response to a bindless descriptor and uses that. For example:

```
void main()
{
    uint _43 = AllocateInvocationID();
    InvocationID = _43;

    // Load the "BDA" from RTAS heap.
    uvec2 _52 = DescriptorHeapRobustness.descriptors[registers._m4]._m0[0u];
    // Extract the stride.
    uint _54 = _52.y >> 16u;
    // Compute descriptor size for robustness purposes. We have to skip out of bounds load-store.
    uint _57 = uint(imageSize(_27[registers._m4]));

    bool _466 = ValidateBDALoadStore(
        BDA = _52,
        offset = gl_GlobalInvocationID.x * _54,
        size = _54,
        type = 0u,
        identifier = InvocationID,
        in_bounds = gl_GlobalInvocationID.x < _57);

    // Report if failure
    AssumeTrue(_466, instruction = 2u);
}
```

## CPU side

The vkd3d-proton implementation here is reasonably straight forward.

- On a `SHADER_READ/WRITE` barrier, clear out the entire bloom buffer.
  Clearing out 100+ MiB every barrier is *intense*, but it runs at interactive rates, so there's that!
  - Also, vkCmdFillBuffer at `offset = size - 8` is used to update the dispatch ID.
    Combined with breadcrumbs_trace we can look at the commands after the fact.
    This can help narrow down if the issue is caused by a cross-dispatch sync bug, intra-dispatch sync bug or likely false positive.
- If sync or sync-compute modes are used for any shader, just allocate more memory for the buffer.

### Buffer partitioning

Since we know the control buffer is allocated with specific sizes, we can extract the different regions with some simple bit
twiddling:

E.g. for a buffer that's 0x1000408 bytes:

- The last 8 bytes is reserved for dispatch ID and atomic counter
- The first 0x400 bytes is the control block for expect-assume in general
- The trailing 0x1000000 bytes after the control block is the bloom filter buffer

```
uint hash_mask = bitfieldExtract(4294967295u, int(0u), int(uint(findMSB(uint(BloomBuffer.atomics.length())))));
uint hash_offset = bitfieldExtract(uint(BloomBuffer.atomics.length()), int(0u), int(uint(findMSB(uint(BloomBuffer.atomics.length()))))) - 1u;
```

### Controlling the bloom buffer size

`VKD3D_BLOOM_BUFFER_SIZE_LOG2=26` for a 64 MiB buffer. This can be raised if false positive rate is too high.
It has *significant* impact on runtime performance.