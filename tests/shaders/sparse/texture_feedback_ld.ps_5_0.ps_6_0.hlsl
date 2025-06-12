Texture2D<float4> tex : register(t0);

cbuffer args : register(b0)
{
    float lod;
};

void main(float4 pos : SV_POSITION, out float4 o0 : SV_TARGET0, out uint o1 : SV_TARGET1)
{
    uint fb;
    o0 = tex.Load(int3(pos.xy, int(lod)), int2(0, 0), fb);
    o1 = CheckAccessFullyMapped(fb) ? 1 : 0;
}