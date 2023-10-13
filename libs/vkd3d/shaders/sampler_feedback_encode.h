#ifndef SAMPLER_FEEDBACK_ENCODE_MIN_MIP_H_
#define SAMPLER_FEEDBACK_ENCODE_MIN_MIP_H_

layout(push_constant, std430) uniform Registers
{
	ivec2 src_offset;
	ivec2 dst_offset;
	ivec2 resolution;
	int src_mip;
	int dst_mip;
};

layout(set = 0, binding = 0, rg32ui) uniform uimage2DArray Output;

void sampler_feedback_encode_min_mip(ivec2 coord, int layer, uint value)
{
	// Fixup bogus inputs.
	// Since max resource size in D3D12 is 16k, we have a maximum number of 15 mips.
	// This leaves 4 bits to store encoded min-mip in the 64-bit MSB.
	// When we roundtrip with a decode we can get proper behavior (invariant),
	// unlike NV which is completely broken here on native drivers.
	// If we're given bogus input, like mip levels above a certain limit,
	// AMD native will clip if given bogus inputs, so we try to match AMD behavior here.

	if (value > 14u)
		value = 0xff;

	uvec2 encoded_value = uvec2(0u, value == 0xff ? 0 : (value << 28));
	imageStore(Output, ivec3(coord + dst_offset, layer), encoded_value.xyxy);
}

void clear_bit(inout uvec2 v, int bit)
{
	// Could bitcast to 64-bit, but, eeeeh.
	if (bit >= 32)
		v.y &= ~(1u << (bit - 32));
	else
		v.x &= ~(1u << bit);
}

void set_bit(inout uvec2 v, int bit)
{
	if (bit >= 32)
		v.y |= 1u << (bit - 32);
	else
		v.x |= 1u << bit;
}

// When resolving back-to-back mips, we'll need barriers. Could in theory use 64-bit atomics here,
// but we'll have to refactor a bunch of stuff to be able to use 64-bit atomics, and feels kinda overkill
// to hammer a ton of atomics here ...
void sampler_feedback_encode_mip_used(ivec2 coord, int layer, bool accessed)
{
	// This gets awkward.
	uvec2 value = imageLoad(Output, ivec3(coord + dst_offset, layer)).xy;
	int bit_offset = dst_mip * 4;

	if (accessed)
		set_bit(value, bit_offset);
	else
		clear_bit(value, bit_offset);

	// Clear out any neighbor bits. After an encode we only want the "self" bits to remain.

	// If we're encoding the adjacent horizontal value, clear that bit too.
	if (coord.x + 1 < resolution.x)
		clear_bit(value, bit_offset + 1);
	// If we're encoding the adjacent vertical value, clear that bit too.
	if (coord.y + 1 < resolution.y)
		clear_bit(value, bit_offset + 2);
	// If we're encoding the adjacent diagonal value, clear that bit too.
	if (all(lessThan(coord + 1, resolution)))
		clear_bit(value, bit_offset + 3);

	imageStore(Output, ivec3(coord + dst_offset, layer), value.xyxy);

	if (coord.x > 0 && dst_offset.x > 0)
	{
		// Clear out usage from left neighbor.
		value = imageLoad(Output, ivec3(coord + dst_offset - ivec2(1, 0), layer)).xy;
		clear_bit(value, bit_offset + 1);
		imageStore(Output, ivec3(coord + dst_offset - ivec2(1, 0), layer), value.xyxy);
	}

	if (coord.y > 0 && dst_offset.y > 0)
	{
		// Clear out usage from top neighbor.
		value = imageLoad(Output, ivec3(coord + dst_offset - ivec2(0, 1), layer)).xy;
		clear_bit(value, bit_offset + 2);
		imageStore(Output, ivec3(coord + dst_offset - ivec2(0, 1), layer), value.xyxy);
	}

	if (all(greaterThan(uvec4(coord, dst_offset), uvec4(0))))
	{
		// Clear out usage from top-left neighbor.
		value = imageLoad(Output, ivec3(coord + dst_offset - ivec2(1), layer)).xy;
		clear_bit(value, bit_offset + 3);
		imageStore(Output, ivec3(coord + dst_offset - ivec2(1), layer), value.xyxy);
	}
}

#endif

