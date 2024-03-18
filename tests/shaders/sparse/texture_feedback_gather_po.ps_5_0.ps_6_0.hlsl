SamplerState samp : register(s0);
Texture2D<float4> tex : register(t0);

cbuffer args : register(b0)
{
    float2 offset;
};

void main(float4 pos : SV_POSITION, float2 uv : UV_TEXCOORD, out float4 o0 : SV_TARGET0, out uint o1 : SV_TARGET1)
{
    uint fb;
    int2 ofs = int2(offset);
    o0 = tex.GatherRed(samp, uv, ofs, ofs, ofs, ofs, fb);
    o1 = CheckAccessFullyMapped(fb) ? 1 : 0;
}