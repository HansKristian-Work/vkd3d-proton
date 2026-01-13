# Tiler Optimization Extensions

To allow for optimal rendering performance on GPUs which support tiling, vkd3d-proton
provides some special interfaces to expose this style of rendering by keeping render targets on-chip,
allowing framebuffer fetch mechanisms which default D3D12 APIs do not expose by default.

Applications which intend to run D3D12 over Proton on mobile hardware such as Adreno
are able to leverage this new interface with minimal changes to the renderer.
The expectation is that this interface will mostly be relevant for VR games
which are more likely to cater to mobile concerns.

## Alternative to D3D12 RenderPass API

The default [D3D12 RenderPass API](https://microsoft.github.io/DirectX-Specs/d3d/RenderPasses.html)
supports extensions for tiler optimizations with APIs like
`D3D12_RENDER_PASS_ENDING_ACCESS_PRESERVE_LOCAL_SRV`.
However, this API is fundamentally incompatible with Vulkan, and it is also unimplementable by
virtually all mobile hardware, including mobile hardware that vkd3d-proton cares about.
This vkd3d-proton interface should be supportable on a wide range of hardware.

It relies on [VK_KHR_dynamic_rendering_local_read](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_dynamic_rendering_local_read.html)
as well as [VK_KHR_unified_image_layouts](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_unified_image_layouts.html).

### Additional programmable blending support

Unlike D3D12's RenderPass API, this extended API can express programmable blending
where an attachment can be sampled from even when in `D3D12_RESOURCE_STATE_RENDER_TARGET`
or `D3D12_RESOURCE_STATE_DEPTH_WRITE` resource states.

### Shader reuse

The intent is that existing shaders can be reused.
For example, this shader could be used for programmable blending:

```
Texture2D<float4> RenderTarget : register(t5, space6);

float4 blend(float4 dst, float4 src)
{
    // Or whatever you want.
    return lerp(dst, src, src.a);
}

float4 main(float4 pos : SV_Position, float4 color : COLOR) : SV_Target
{
    float4 RT = RenderTarget.Load(int3(pos.xy, 0));
    return blend(RT, color);
}
```

where we have new APIs for remapping `t5, space6` to e.g. RTV #0 in the root signature.
Applications can also redirect normal Texture2D SRVs to the bound depth or stencil attachments.
This allows for typical deferred rendering scenarios where the G-buffer is read from on-chip memory instead
of textures which saves significant memory bandwidth.
Multi-sampled input attachments are also supported. This can be used for e.g. custom HDR resolves on-chip.

Layered rendering and view instancing is also supported.
However, in this case, a non-arrayed `Texture2D` or `Texture2DMS` is still used in the shader.
The implementation samples from the corresponding layer implicitly.

### `OMSetRenderTargets` support

This new interface is compatible with both "immediate" `OMSetRenderTargets` style rendering
as well as the more dedicated RenderPass APIs. However, to be as tiler friendly as possible,
it is recommended to use the RenderPass API to get the most out of this interface since
it can express e.g. discarding G-buffer attachments which may no longer
need to remain valid after lighting pass is done.

## New Device APIs

```
typedef struct D3D12_VK_INPUT_ATTACHMENT_MAPPING
{
    UINT RegisterSpace;
    UINT ShaderRegister;
} D3D12_VK_INPUT_ATTACHMENT_MAPPING;

typedef struct D3D12_VK_INPUT_ATTACHMENT_MAPPINGS
{
    UINT NumRenderTargets;
    D3D12_VK_INPUT_ATTACHMENT_MAPPING RenderTargets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    BOOL EnableDepth;
    BOOL EnableStencil;
    D3D12_VK_INPUT_ATTACHMENT_MAPPING Depth;
    D3D12_VK_INPUT_ATTACHMENT_MAPPING Stencil;
} D3D12_VK_INPUT_ATTACHMENT_MAPPINGS;

typedef enum D3D12_VK_TILER_OPTIMIZATION_TIER
{
    D3D12_VK_TILER_OPTIMIZATION_NOT_SUPPORTED = 0,
    D3D12_VK_TILER_OPTIMIZATION_TIER_1 = 1,
} D3D12_VK_TILER_OPTIMIZATION_TIER;

[
    uuid(b7798d22-9fce-434d-8eeb-c3cef1056125),
    object,
    local,
    pointer_default(unique)
]
interface ID3D12DeviceExt2 : ID3D12DeviceExt1
{
    D3D12_VK_TILER_OPTIMIZATION_TIER GetTilerOptimizationTier();
    HRESULT OptInToTilerOptimizations();
    UINT GetInputAttachmentDescriptorsCount();
    HRESULT CreateRootSignatureWithInputAttachments(
            UINT node_mask,
            const void *bytecode, SIZE_T bytecode_length,
            const D3D12_VK_INPUT_ATTACHMENT_MAPPINGS *mappings,
            REFIID riid, void **root_signature);
    void CreateInputAttachmentDescriptors(
            UINT render_target_descriptor_count,
            const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors,
            BOOL single_descriptor_handle,
            const D3D12_CPU_DESCRIPTOR_HANDLE *depth_descriptor,
            const D3D12_CPU_DESCRIPTOR_HANDLE *stencil_descriptor,
            D3D12_CPU_DESCRIPTOR_HANDLE base_descriptor);
}
```

### `GetTilerOptimizationTier()`

This is a simple query to check if these APIs are supported by the device.
There is currently only one feature tier.

### `HRESULT OptInToTilerOptimizations()`

This is intended to be called right after the ID3D12Device is created or
as early as possible in the lifetime of the device.
Calling this modifies the implementation in certain ways to make it compatible with
tiler optimizations without adding a lot of extra API churn which would clutter an application.
This call is not thread-safe and should not be called concurrently with any other API command.

The differences are:

- If a resource is created with `ALLOW_RENDER_TARGET` or `ALLOW_DEPTH_STENCIL`
  and the image can be sampled from (i.e., no `D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE`),
  `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` is added automatically.
- When creating RTV views, `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` is added automatically.
- When creating DSV views, `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` is added automatically in some cases:
  - For DSV views with a single plane, the usage is added automatically.
  - For DSV views with both planes, the DSV is only input attachment enabled if
    there is exactly one plane which is marked read-only with
    `D3D12_DSV_FLAG_READ_ONLY_DEPTH` or `D3D12_DSV_FLAG_READ_ONLY_STENCIL`.
    The read-only aspect is compatible with input attachments.
    (This is somewhat awkward, but it removes a lot of extra API churn, and is very unlikely to come up in practice).

NOTE: If the application needs to use both depth and stencil input attachments at the same time,
two DSVs can be created, one with read-only depth, and the other with read-only stencil. The
separate DSVs can then be passed to `CreateInputAttachmentDescriptors()`.

### `UINT GetInputAttachmentDescriptorsCount()`

To be able to read from on-chip memory, the application allocates special SRVs in the descriptor heap.
Rather than normal texture SRVs, Vulkan requires the use of `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`,
so the normal `CreateShaderResourceView()` will not work.

When writing descriptors to the heap, multiple descriptors are written together as a group in
a layout which is opaque to the application. The expectation is that 10 CBV_SRV_UAV descriptors
are consumed (8 RT + Depth + Stencil), but it may be different due to descriptor packing concerns.
(`vkGetDescriptorSetLayoutSizeEXT()` determines the number of descriptors.)

For simplicity and practicality of the implementation, the number of descriptors is fixed at the upper bound.
New input attachment descriptors need only be allocated once per render pass, and a few extra wasted descriptors
should not be a major concern.

### `HRESULT CreateRootSignatureWithInputAttachments()`

This is equivalent to `CreateRootSignature()`, except that extra information can be added
for input attachments. `mappings` can be `NULL` in which case the call is equivalent
to `CreateRootSignature()`. (It is not reasonable to modify the encoded RootSignature payload to
hack in support for this, so this was determined to be the most practical solution.)

Input attachment mappings only work for non-arrayed descriptors. I.e., shaders which access
the bound attachments through bindless means will not work with this interface
since the compiler needs to statically map resource variables to a render target index to
be able to take advantage of on-chip data.

Input attachment mappings can conflict with normal descriptor table bindings,
i.e. override existing descriptor table bindings in the root signature.
In this case, the input attachment mapping takes precedence.
This allows applications to keep using the normal SRV path on most implementations,
but selectively "opt-in" to the fast path when supported without having to modify the shader code.

When a `Texture2D` or `Texture2DMS` is mapped to an input attachment, that texture must only be used
with simple `::Load()` functions or equivalent. It cannot be used with a sampler object.
Misuse will lead to PSO creation failure.

The coordinate except for sampler index is ignored, and replaced with the current pixel coordinate.
To make this transformation transparent, the pixel shader can sample from `int2(SV_Position.xy)` as the fallback.

When mappings are used, the root signature must have at least one 1 DWORD available in the root signature
for the implementation to pass down additional data.

### `void CreateInputAttachmentDescriptors()`

Takes an equivalent of `OMSetRenderTargets` and writes input attachment descriptors of them.
`GetInputAttachmentDescriptorsCount()` number of consecutive CBV_SRV_UAV descriptors are clobbered.

The main difference is that depth and stencil descriptors are separate in this interface.
The RTV or DSV descriptors need not be the exact same `D3D12_CPU_DESCRIPTOR_HANDLE` passed to `OMSetRenderTargets()`,
but they must be equivalent except for any read-only DSV state.

TODO: Add an interface for RenderPass API desc as well?

NULL RTVs or DSVs are ignored, and the matching descriptor in the heap is not modified.
Using input attachments to sample from a NULL RTV or DSV is undefined behavior.
Just use normal SRVs instead.

### PSO considerations

An input attachment which intends to read from a render target must define that render target
in the PSO by using a sufficiently large `NumRenderTargets`.
If an SRV is mapped to render target `N`, and `N` is greater-or-equal to `NumRenderTargets`,
the input attachment must not be read from.

The RTV format can be `DXGI_FORMAT_UNKNOWN` if the render target is only used as an input attachment
in the PSO.

Depth-stencil input attachments can sample from input attachments even with `DSVFormat` equal to `DXGI_FORMAT_UNKNOWN`.

## New CommandList APIs

```
[
    uuid(9c228166-bf9e-464c-9078-ecf20a13271a),
    object,
    local,
    pointer_default(unique)
]
interface ID3D12GraphicsCommandListExt2 : ID3D12GraphicsCommandListExt1
{
    void InputAttachmentPixelBarrier();
    void SetRootSignatureInputAttachments(D3D12_GPU_DESCRIPTOR_HANDLE handle);
    void SetInputAttachmentFeedback(UINT render_target_concurrent_mask, BOOL depth_concurrent, BOOL stencil_concurrent);
}
```

### `void InputAttachmentPixelBarrier()`

While an image is in `RENDER_TARGET` or `DEPTH_WRITE` resource states
(or equivalent `D3D12_BARRIER_LAYOUT_RENDER_TARGET` or `D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE`),
it cannot be sampled from as an input attachment without performing a per-pixel barrier.
This can be called at any time, even inside a render pass.
Only render target writes before the pixel barrier are visible to input attachment reads after the barrier.

NOTE: Unlike D3D12, Vulkan supports this use case in the `VK_IMAGE_LAYOUT_GENERAL` image layout,
which is why this feature requires `VK_KHR_unified_image_layouts`.
This is a pure memory barrier, and not a layout transition.

### `void SetRootSignatureInputAttachments()`

Binds the descriptors for input attachments for graphics pipelines.
Unlike normal root parameters, this argument is never invalidated by binding new graphics root signatures.
It also does not need a root parameter index.
It can safely be called once per OMSetRenderTargets and forgotten about.
The descriptor handle must point to the currently bound descriptor heap.

### `void SetInputAttachmentFeedback()`

Programmable blending use cases and G-buffer deferred rendering are similar, but have different data access patterns.

In programmable blending, there is concurrent access of the render target while sampling from it.
Even with appropriate barriers in place, there may be hazards when render targets are compressed,
(as compressed render targets typically operate on some block structure)
leading to garbage pixels being read in the input attachment unless the implementation knows about this case up front.
In typical G-buffer deferred rendering there is no such issue since the data flow is clearly separated by a writing phase,
then a read-only phase.

By default, feedback is disabled. For render targets, if bit N is set in the mask, feedback is enabled for render target N.

IMPORTANT: Calling this will end the render pass internally, so this should not be called last-minute while inside a render pass.
For performance, set this state up front, alongside `OMSetRenderTargets()` or right before `BeginRenderPass()`.
If you don't know up front, just enable full feedback for the render pass.
It should be fine on most implementations anyway.

#### Note on framebuffer coherency

The input attachments in this extension do not support fully coherent framebuffers and input attachments,
meaning that attempting programmable blending with overlapping geometry will not work, even
when feedback is enabled.

To outline the "levels" of input attachment access and what is and isn't supported:

##### Simple case, no feedback needed (supported in TIER_1)

- Write to attachment
- InputAttachmentPixelBarrier
- Only read from attachment via input attachment (this is basically an SRV now)

##### Basic programmable blending, feedback needed (supported in TIER_1)

- Render full-screen quad while reading from the attachment in the shader at the same time
- InputAttachmentPixelBarrier
- Render full-screen quad while reading from the attachment in the shader at the same time
- InputAttachmentPixelBarrier
- Render full-screen quad while reading from the attachment in the shader at the same time
- ...

##### Fully coherent programmable blending (not supported in TIER_1)

- Render complex mesh with overlapping geometry, each fragment achieving correct programmable blending.

Vulkan supports the fully coherent use case through `VK_EXT_rasterization_order_attachment_access`,
but this is supported only by a few select mobile IHVs. Could be exposed through a TIER_2 in theory at some point
if need be.
