# Sampler Feedback

This document aims to explain the implementation details of sampler feedback emulation.

## Implementation goal

Implement as much as we can without having to pass down side-band information
about either feedback texture or sampler heap.
It is possible in theory to pass this down, but it is a ton of work
that we will only consider if we are forced to,
i.e. a game relies on sampler feedback to work and relies on this information to look correct.

## Broken native drivers

All native drivers from NV, AMD and Intel are non-compliant with spec and fall down
once you venture out of the most trivial test cases imaginable, and even then
the hardware behavior is deeply questionable in places on all vendors.
This document will try to outline all known issues.

## Format

The `DXGI_FORMAT_SAMPLER_FEEDBACK_*` formats are opaque formats that have no
app visible layout. They can only be interacted with in certain ways:

- UAV clear (clear color is ignored, and clear rect is ignored)
- `WriteSamplerFeedback` in shader through a special `FeedbackTexture` type.
- CopyResource (full resource copy)
- ResolveSubresourceRegion ENCODE and DECODE
- The transcoded format is `R8_UINT`

## MipRegion

In `D3D12_RESOURCE_DESC1` we have mip region sizes.
For each mip, we group NxM texels together into one mip region texel.
When the mip level dimensions don't align, we're supposed to round up
so that the edges have less than NxM texels.

In principle, when we access normalized coordinate `n` in LOD `l`, we compute
the resulting coordinate as:

```
vec2 unnormalized = n * textureSize(paired, l) / mip_region_size;
ivec2 snapped = ivec2(floor(unnormalized));
```

We need to know the mip region size in the shader.
To implement this, we abuse the fact that the feedback image is opaque
and we control the size of the image. The lower 4 bits of the feedback texture
holds the width and height of MipRegion. We just need to ensure the resulting
image is large enough so that we satisfy `feedback_width >= ceil(width / region_width)`.

In the shader, we decode the mip width as:

```
ivec2 region_log2 = -(imageSize(feedback) & 15);
```

This exponent can conveniently be used with `ldexp` to rescale coordinates into mip region space.

## Sampler wrapping modes

Only `WRAP` and `CLAMP` are supported, but as we will see later, arguably only `CLAMP` is supported due to bugs.
We have no knowledge about `WRAP` or `CLAMP` mode, so we use a simple heuristic.

The coordinate is assumed to be wrapped. When games use `CLAMP` mode,
they don't tend to intentionally sample outside the `[0, 1)` range.
They only use `CLAMP` to avoid filtering in a wrapped way.
When we evaluate filter extents, we assume `CLAMP`.
This is assumed to be good enough in practice.
Again, `WRAP` is basically broken on NVIDIA anyway ... :')

## Neighbor block access

When filtering, we can access more than one mip region.
Rather than introducing a loop to write out NxM regions,
we perform one atomic update where we can flag neighbor access as well if needed.
4 bits is required for this:

- Self access
- Horizontal neighbor access
- Vertical neighbor access
- Diagonal neighbor access

Since mip-mapped storage images are not universally supported,
we place all mip levels on the same single-mip image where we just shift the bit offsets.
With 14 as the maximum valid mip level we can access (16k x 16k image being maximum in D3D12),
we can fit this into 64-bits, which means 64-bit atomics are required for this emulation feature.
(It's theoretically possible to support this on 32-bit atomics by using 2 layers per layer instead,
but it's kinda annoying.)

To query if mip region is in use, we just need a simple gather
and propagate the bits as necessary:

```
bool fetch_mip(vec2 unnormalized_feedback_coord, int mip, float layer)
{
	// Sample such that we get bottom-right (Y) as target region,
	// and the potential neighbors in top-left (W), left (X) and up (Z).
	// If we detect any writes in those neighbors, propagate them to the bottom-right.
	// Use -1 constant offset to avoid having to compute that offset outside,
	// which makes the code more awkward.

#ifdef ARRAY
	vec3 fcoord = vec3(unnormalized_feedback_coord * inv_feedback_resolution, layer);
#else
	vec2 fcoord = unnormalized_feedback_coord * inv_feedback_resolution;
#endif

	uvec4 values;
	if (mip >= 8)
	{
		values = textureGather(Input, fcoord, 1);
		mip -= 8;
	}
	else
	{
		values = textureGather(Input, fcoord, 0);
	}

	// Shift down to get the mip level we care about. Every mip gets 4 bits of data.
	// This is enough for 16 mips.
	uvec4 per_mip_bits = bitfieldExtract(values, 4 * mip, 4);

	// Check self-access and neighbor access.
	// Clamp behavior is okay here. If we intend sample region (0, 0)
	// there cannot be neighbor access unless we also have self-access,
	// so false positive is not possible here.

	const uint HORIZ = 2;
	const uint VERT = 4;
	const uint DIAG = 8;
	const uint SELF = 1;
	return any(notEqual(per_mip_bits & uvec4(HORIZ, SELF, VERT, DIAG), uvec4(0)));
}
```

### Caveats

This can break in some extreme cases like 4x4 mip region and 16x aniso where more than 2x2 blocks can be accessed,
but this case is ignored. If this case has to be supported, we can force a 16x16 mip region
and just duplicate blocks to pretend we have 4x4 mip regions.

Sampler feedback's main feature is in accurate streaming, where mip regions tend to
be the sparse tile size anyway, where this problem is irrelevant.

## Resolving MinMip

MinMip allows for two implementation strategies,
see [docs for details](https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html#decoded-representation).
Either the MinMip access is updated in-place in the top level region,
or a coarse LOD access can "smear". AMD's implementation does the first, and NVIDIA and Intel implement the "smear" approach.
The smear approach is very convenient for us.

When we record a feedback write in vkd3d-proton,
we write in the coordinate system per-LOD, not top-level LOD.
On resolve, this region gets expanded out as we rescale the region.

With NPOT texture sizes, we have to be very careful on how we implement the MinMip resolve
since there can be overlap. I.e., an access in LOD 1 can touch a 3x3 region in LOD 0 space.
This is tested in our test suite and we appear to have exact same behavior now as NVIDIA and Intel.

## Resolving MipUsed

This is trivial, since we just check for access per LOD and write out 0xff or 0 accordingly.
dxil-spirv's implementation does not care about MipUsed or MinMip at all.

## Implementing WriteSamplerFeedback

To have a working sampler feedback we need to know some things:

- Which integer LOD is accessed
- What is the filter extent, i.e. which mip regions are actually touched?
- Is there tri-linear filtering active?

This can be somewhat hard to determine depending on the opcode used.

### WriteSamplerFeedback (implicit LOD)

Clamped QueryLOD gives us an accurate view of accessed LOD. If tri-linear is used, it can be fractional.
If tri-linear is not used the reported LOD will be rounded accordingly.

For filter extent, we don't know if the filtering mode is POINT, LINEAR or ANISOTROPIC.
We will be conservative and assume that 16x aniso is used.

Based on the UV gradient, we can compute the maximum filter extent.

```
vec2 ddx = abs(dFdx(UV));
vec2 ddy = abs(dFdy(UV));
vec2 extent = vec2(max(ddx.x, ddy.x), max(ddx.y, ddy.y));
extent *= vec2(textureSize(paired, LOD));
extent *= 0.5; // Center the box around self, and consider +/- extent as filter range.
```

Now we have the extent in pixel amounts.

Based on exhaustive testing, I've found that the maximum filter extent possible is half of this, so e.g.
16x aniso will touch `[-8, +8]` pixels.

Non-POT aniso factors are awkward in that implementations can round up the aniso factor to nearest supported.
This manifested in our testing, where the filter extent can even extend beyond the gradient (what the actual hell)!

To solve this conservatively we need to round the extent to next POT, since POT aniso is supported by everyone.
frexp / ldexp shenanigans implement this in a cute way.
We clamp the extent to be at least 0.5 pixels (bilinear), and at most 8 pixels (16x aniso).
Finally, this is scaled into mip region space where we compute where to write and if we need to flag neighbor access.

#### Undocumented MinMipClamp mode

The docs don't mention it at all, but there is a hidden overload which takes MinMip clamp value.
To implement this, we clamp the queried LOD with the provided clamp. We lose the ability to determine
if the sampling should be tri-linear or not, but we are conservative, so it shouldn't cause serious issues.
This is also only a problem if fractional clamp value is used, which is somewhat goofy.

### WriteSamplerFeedbackBias

Bias is mostly the same, but with some funky details.

Adding bias to QueryLOD would be broken, since we lose out on sampler LOD clamping as well as tri-linear knowledge.
The correct thing to do is to scale the UV by exp2(bias) before calling QueryLOD.

Caveat: We technically have to call QueryLOD 4x times with QuadBroadcast(exp2(bias), lane),
since LOD needs to be computed per-pixel, but current implementation ignores this for sake of our sanity.

Another extremely bizarre result I found in hardware is that using LOD bias will scale the anisotropic extent
as well, which is completely nuts and violates all specs I know of. Our tests verify this result on NV and Intel.
AMD cannot be reliably tested due to an issue that will be explained later ...

### WriteSamplerFeedbackGrad

The Grad implementation is not very good, since we have zero knowledge about sampler here,
so sampler bias and LOD clamping will not happen.
We do our best and compute LOD with bi-linear assumption. Assuming anisotropic would be questionable.
We are not guaranteed to have helper lanes active here, so we cannot abuse the helper lanes and quad ops
to inject QueryLOD either.

### WriteSamplerFeedbackLevel

Explicit level has similar concerns as Grad since we cannot use QueryLOD, but at least we are guaranteed
that aniso cannot be used.

## Encode / Decode pairs

To be correct w.r.t. encode / decode pairs, we reserve the top 4 bits to store encoded min-mip.
This ensures that we don't get smearing on decode since we would not be able to distinguish between
a shader access or encode access.
Since LOD 15 is not valid, we pilfer those 4 bits to store this mip level.

## Native implementation bugs

All implementations are broken in some form and only the most trivial test cases pass everywhere.
Effectively, this D3D12 feature is completely broken, and we feel justified in implementing it in
an emulated way. Only our implementation can pass our test suite unscathed. Of course, we omit test cases
where we know we cannot support it correctly (Grad / Level with non-trivial sampler comes to mind).

The test coverage in D3D12 is ... barely existing and beyond some trivial demos I doubt anyone actually tested this.

### AMD

#### NPOT is completely broken

AMD seems to implement sampler feedback in a very strange way where they
sample from the feedback image instead, and somehow, the accessed texels are implicitly written.
When I looked at disassemblies from RGA, I observe that the descriptor is modified in-place in SGPRs
to shuffle in information about the feedback image and there was no write, only one image_sample instruction.

E.g. if we have a 17x17 image with 4x4 mip region, and UV = 0.5 - epsilon is accessed, we would normally expect
that mip region (2, 2) is accessed since `floor((0.5 - epsilon) * 17 / 4)` is > 2.
However, we observe (1, 1). All tests suggest that the math on AMD is `floor((0.5 - epsilon) * floor(17 / 4))` instead
which is completely broken. Also, when resolving this image, we only get 4x4 texels, not 5x5, so
I'm convinced AMD cannot implement NPOT correctly in hardware. The spec docs even have a concrete example about NPOT,
which was clearly never tested.

#### Extremely over-conservative

A funny side effect of the way the feedback image is constructed,
we get extreme over-conservative behavior when filtering is applied.

For example, even with mip regions of e.g. 64x64, a bilinearly sampled texel can touch a pixel over 32 texels away.
This also reinforces the theory that AMD is directly sampling the low resolution feedback image instead of actually
computing accessed regions. In the low-resolution domain, a bi-linear filter would indeed touch both mip regions,
but definitely not in the actual high-resolution domain the paired image lives in ...

Being overly conservative might not technically be a bug, but it makes the feature kinda useless for accurate streaming.

#### Resolving MinMip with individual layers is broken

The way subresources works with MinMip is esoteric, and they are supposed to be reinterpreted
where we pretend the images have 1 mip level for purposes of computing the subresource.
AMD seems to get this wrong.

#### Implicit LOD with helper lanes is broken

When calling WriteSamplerFeedback where all lanes in a quad are active, but some of them are helpers,
you're supposed to mask the feedback write appropriately.

On RDNA2, the wrong LOD is computed. It is as-if helpers are disabled before the UV can be computed ... E.g.

```
UV = compute_uv_in_quad_uniform_flow();
if (!is_helper) // Inserted by driver since helpers cannot have side effects.
    WriteFeedback(feedback, UV); // UV computation are probably sunk to a branch, leading to garbage LOD.
```

However, their compiler probably forgot to consider that this is a UAV write that is control dependent on helpers,
which is esoteric at best, but it's a thing now in D3D12!

On RDNA3, this is so broken that my GPU instantly hangs when running this test. ;_;

### Intel

#### CopyResource hangs GPU

Enough said ... I suspect they forgot to adjust `D3D12_RESOURCE_DESC1` to account for mip region shrinking image.

#### Stray writes

In some of our tests, we found that Intel hardware will write completely unrelated mip regions in some cases.
Likely a hardware bug, but hard to say. It's reproducible in our NPOT test and not random.
The test is using point filtering, so it cannot be explained by conservative filtering.

#### Does not respect SRV mip level clamping (not really a bug, more like a quirk)

AMD, NV and vkd3d-proton do this, but the D3D12 spec requires that the full mip chain is bound in paired texture.

#### Non-transitive encode / decode

Spec says that ENCODING and then DECODING should give the same result back.
This does not happen on Intel at all, but it seems to smear out results in the round-trip.

#### Overall probably the most compliant implementation

Goofy CopyResource hang aside which is clearly just a driver bug,
the hardware seems the most robust of what I tested.

### NVIDIA

#### WRAP filtering mode is broken

This filtering mode is generally broken.
Interestingly enough, the corresponding Vulkan vendor extension only supports `CLAMP`. Mysterious ...

#### Certain MipRegion sizes are broken

A MipRegion size of e.g. 32x32 broke completely and one of the tests in question only started working
at 64x64 mip region size. Again, the NV vendor extension suggests that 32x32 region is simply not supported
in hardware ... The D3D12 docs require that all possible mip region sizes are supported.

### General issues

#### DstX/DstY is broken

Spec says that these are supported, but it's broken in MIN_MIP mode on all drivers.
In fact, we actively have to ignore DstX/DstY here to match native implementations.

#### Not updated for compute shader derivatives

SM 6.6 added compute shader derivatives, so implicit LOD variants should be supported,
but it was forgotten. :|
