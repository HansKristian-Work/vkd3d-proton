#version 450

#extension GL_GOOGLE_include_directive : require
#define ARRAY
#include "sampler_feedback_decode_min_mip.h"

layout(location = 0) out uint Output;

void main()
{
	// First, fetch access for top mip. This is the simple case since we don't have to consider NPOT footprint spilling.
	ivec2 icoord = ivec2(gl_FragCoord.xy);
	int result = sampler_feedback_decode_min_mip(icoord, float(gl_Layer));
	Output = uint(result);
}
