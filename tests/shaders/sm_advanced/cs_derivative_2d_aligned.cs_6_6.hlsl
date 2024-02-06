Texture2D<float> Tex : register(t0);
RWTexture2D<float4> SOut : register(u0);
SamplerState Samp : register(s0);

[numthreads(8, 8, 1)]
void main(uint2 thr : SV_DispatchThreadID)
{
    float2 uv = float2(thr) / 32.0; // We should sample LOD 2 here.
    float v = round(Tex.Sample(Samp, uv) * 255.0);
    float dx = ddx_fine(uv.x);
    float dy = ddy_fine(uv.y);
    float w = Tex.CalculateLevelOfDetailUnclamped(Samp, uv);
    SOut[thr] = float4(v, dx, dy, w);
}
