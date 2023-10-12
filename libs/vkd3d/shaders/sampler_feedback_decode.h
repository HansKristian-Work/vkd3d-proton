#ifndef SAMPLER_FEEDBACK_DECODE_MIN_MIP_H_
#define SAMPLER_FEEDBACK_DECODE_MIN_MIP_H_

layout(push_constant, std430) uniform Registers
{
	ivec2 src_offset;
	ivec2 dst_offset;
	ivec2 resolution;
	ivec2 paired_resolution;
	vec2 inv_paired_resolution;
	vec2 inv_feedback_resolution;
	int mip_levels;
	int mip_level;
};

#ifdef ARRAY
layout(set = 0, binding = 1) uniform usampler2DArray Input;
#else
layout(set = 0, binding = 1) uniform usampler2D Input;
#endif

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

int sampler_feedback_decode_min_mip(ivec2 icoord, float layer)
{
	// Use a small epsilon here so that POT scaling does not add false positives, or we get funny rounding errors.
	// Aim to sample at corners, but not *exactly* at corners to avoid straddling into regions we don't really care about.
	const float EPSILON = 1.0 / (256.0 * 256.0);
	vec2 top_left_coord = vec2(icoord) + EPSILON;
	vec2 bottom_right_coord = top_left_coord + (1.0 - 2.0 * EPSILON);

	// Compute the relevant footprint.
	vec2 top_left_coord_normalized = top_left_coord * inv_paired_resolution;
	vec2 bottom_right_coord_normalized = bottom_right_coord * inv_paired_resolution;

	// Flooring gets us the behavior we want.
	// We'll sample exactly in-between 4 texel centers such that coordiate we floor becomes the bottom-right texel.

	if (fetch_mip(vec2(icoord), 0, layer))
		return 0;

	int result = 0xff;
	for (int i = 1; i < mip_levels; i++)
	{
		// We need to intersect the LOD 0 region with the used region in a coarse LOD.
		// For simple POT, this is a trivial 2x2 expansion.
		// However, for NPOT, this gets ... hairy very quickly.
		// It's possible that a lower LOD access can cause a 3x3 access in the higher LOD.
		// Sample the lower LOD at each (approximate) corner to get a conservative estimate of access.
		// This is intended to match NV behavior.

		// Rescale the coordinates into coordinates matching lower resolution.
		vec2 mip_resolution = vec2(max(paired_resolution >> i, ivec2(1)));
		vec2 c0 = floor(mip_resolution * top_left_coord_normalized);
		vec2 c1 = floor(mip_resolution * bottom_right_coord_normalized);

		if (fetch_mip(c0, i, layer))
		{
			result = i;
			break;
		}

		if (any(notEqual(c0, c1)))
		{
			// Should only trigger for NPOT.
			if (fetch_mip(vec2(c1.x, c0.y), i, layer))
			{
				result = i;
				break;
			}

			if (fetch_mip(vec2(c0.x, c1.y), i, layer))
			{
				result = i;
				break;
			}

			if (fetch_mip(vec2(c1.x, c1.y), i, layer))
			{
				result = i;
				break;
			}
		}
	}

	return result;
}

bool sampler_feedback_decode_mip_used(ivec2 icoord, int mip, float layer)
{
	// This is much simpler.
	return fetch_mip(vec2(icoord + src_offset), mip, layer);
}

#endif

