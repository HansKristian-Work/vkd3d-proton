RWStructuredBuffer<float> RW : register(u0);
Texture2D<float> T : register(t0);
SamplerState S : register(s0);

cbuffer Buf : register(b0)
{
	float2 uv;
	float uv_stride;
};

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	RW[thr] = T.GatherRed(S, uv + uv_stride * float(thr)).w;
}
