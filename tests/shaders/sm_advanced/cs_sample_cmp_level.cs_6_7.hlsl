Texture2D<float> D : register(t0);
SamplerComparisonState S : register(s0);
RWTexture2D<float> U : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    float ref_value = float(id.x) / 8.0;
    float mip = 1.0 / 16.0 + 4.0 * float(id.y) / 8.0;
    float v = D.SampleCmpLevel(S, 0.5.xx, ref_value, mip);
    U[id] = v;
}
