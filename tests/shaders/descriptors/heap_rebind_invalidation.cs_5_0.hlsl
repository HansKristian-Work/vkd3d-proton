RWStructuredBuffer<float> RWBuf : register(u0);
Texture2D<float> Tex : register(t0);
SamplerState Samp : register(s0);
Texture2D<float> TexDummy : register(t6);

[numthreads(4, 4, 1)]
void main(uint2 thr : SV_DispatchThreadID)
{
	float2 uv = -0.25.xx;
	uv += float2(thr) * 0.5;
	RWBuf[thr.y * 4 + thr.x] = Tex.SampleLevel(Samp, uv, 0.0) + 10.0 + TexDummy.Load(int3(0, 0, 0));
}
