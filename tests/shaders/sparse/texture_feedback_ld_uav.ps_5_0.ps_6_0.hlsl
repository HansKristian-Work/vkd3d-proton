RWTexture2D<unorm float4> tex : register(u2);

void main(float4 pos : SV_POSITION, out float4 o0 : SV_TARGET0, out uint o1 : SV_TARGET1)
{
    uint fb;
    o0 = tex.Load(int2(pos.xy), fb);
    o1 = CheckAccessFullyMapped(fb) ? 1 : 0;
}