RWStructuredBuffer<float4> Outputs : register(u0);
Texture2D<float4> T : register(t0);
SamplerState Samplers[] : register(s0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Outputs[thr] = T.SampleLevel(Samplers[NonUniformResourceIndex(thr)], 1.5.xx, 0.0);
}
