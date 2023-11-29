#version 450

#extension GL_GOOGLE_include_directive : require
#define ARRAY
#include "sampler_feedback_decode.h"

layout(location = 0) out uint Output;

void main()
{
	ivec2 icoord = ivec2(gl_FragCoord.xy) - dst_offset; // Compensate for viewport offset.
	bool result = sampler_feedback_decode_mip_used(icoord, mip_level, float(gl_Layer));
	Output = result ? 0xffu : 0u;
}
