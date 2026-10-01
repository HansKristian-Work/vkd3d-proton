RWStructuredBuffer<float> Outputs;

[numthreads(1024, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	// Test every possible "FP24" value and see how it behaves.
	float v = asfloat(thr * 256);
	Outputs[thr] = float(float16_t(v));
}
