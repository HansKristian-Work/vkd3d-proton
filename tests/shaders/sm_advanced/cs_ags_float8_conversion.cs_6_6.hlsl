RWStructuredBuffer<uint4> Outputs : register(u0);
StructuredBuffer<uint4> Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(512, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	uint4 inputs = Inputs[thr];

	uint4 non_sat, sat;
	non_sat.x = asuint(Float8ToFloat32(inputs.x));
	non_sat.y = asuint(BFloat8ToFloat32(inputs.y));
	non_sat.z = Float32ToFloat8(asfloat(inputs.z));
	non_sat.w = Float32ToBFloat8(asfloat(inputs.w));

	sat.x = asuint(Float8ToFloat32Sat(inputs.x));
	sat.y = asuint(BFloat8ToFloat32Sat(inputs.y));
	sat.z = Float32ToFloat8Sat(asfloat(inputs.z));
	sat.w = Float32ToBFloat8Sat(asfloat(inputs.w));

	Outputs[2 * thr + 0] = non_sat;
	Outputs[2 * thr + 1] = sat;
}

