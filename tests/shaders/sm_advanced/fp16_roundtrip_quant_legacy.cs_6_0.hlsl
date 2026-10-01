RWStructuredBuffer<float> Outputs;

[numthreads(1024, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	// Test every possible "FP24" value and see how it behaves.
	float v = asfloat(thr * 256);
	uint p = f32tof16(v);
	precise float up = f16tof32(p);
	Outputs[thr] = up;
}
