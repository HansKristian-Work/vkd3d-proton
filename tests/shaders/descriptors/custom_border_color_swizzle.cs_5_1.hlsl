RWStructuredBuffer<float4> Outputs : register(u0);
Texture2D<float4> T[] : register(t0);
SamplerState Sampler : register(s0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Outputs[thr] = T[NonUniformResourceIndex(thr)].SampleLevel(Sampler, 1.5.xx, 0.0);
}
