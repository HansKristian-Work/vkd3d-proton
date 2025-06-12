Texture2D<float> Tex : register(t0);
RWTexture2D<float4> SOut : register(u0);
SamplerState Samp : register(s0);

// Pretend we're 2D. Since groupsize is not aligned, we get 1D derivatives.
uint2 rewrite_id(uint2 orig)
{
    uint sub_quad_x = orig.x & 1u;
    uint sub_quad_y = (orig.x >> 1u) & 1u;

    uint quad_x = orig.x >> 2u;
    uint quad_y = orig.y;

    // Ensure we maintain square-ish dispatch.
    quad_x = 2 * quad_x + (orig.y & 1u);
    quad_y >>= 1u;

    uint2 coord = uint2(quad_x, quad_y) * 2 + uint2(sub_quad_x, sub_quad_y);
    return coord;
}

[numthreads(8, 9, 1)]
void main(uint2 thr : SV_DispatchThreadID)
{
    thr = rewrite_id(thr);
    float2 uv = float2(thr) / 32.0; // We should sample LOD 2 here.
    float v = round(Tex.Sample(Samp, uv) * 255.0);
    float dx = ddx_fine(uv.x);
    float dy = ddy_fine(uv.y);
    float w = Tex.CalculateLevelOfDetailUnclamped(Samp, uv);
    SOut[thr] = float4(v, dx, dy, w);
}
