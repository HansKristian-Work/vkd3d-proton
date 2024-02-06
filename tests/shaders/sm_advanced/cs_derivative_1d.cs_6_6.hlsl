Texture2D<float> Tex : register(t0);
RWTexture2D<float4> SOut : register(u0);
SamplerState Samp : register(s0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    uint2 coord = uint2(
        (thr & 13u) | ((thr >> 3u) & 2u),
        ((thr >> 5u) << 1u) | ((thr >> 1u) & 1u));
    float2 uv = float2(coord) / 32.0;
    float v = round(Tex.Sample(Samp, uv) * 255.0);
    float dx = ddx_fine(uv.x);
    float dy = ddy_fine(uv.y);
    float w = Tex.CalculateLevelOfDetailUnclamped(Samp, uv);
    SOut[coord] = float4(v, dx, dy, w);
}
