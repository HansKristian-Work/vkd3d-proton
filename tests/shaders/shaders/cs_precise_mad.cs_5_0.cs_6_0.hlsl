RWStructuredBuffer<float> Outputs : register(u0);
StructuredBuffer<float3> Inputs : register(t0);

[numthreads(3, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	float3 v = Inputs[thr];
	precise float res = mad(v.x, v.y, v.z);
	Outputs[thr] = res;
}
