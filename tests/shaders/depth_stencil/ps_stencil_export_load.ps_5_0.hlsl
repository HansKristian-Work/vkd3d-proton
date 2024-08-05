Texture2D<uint4> tex : register(t0);

uint4 main(float4 pos : SV_Position) : SV_TARGET
{
    return tex[uint2(pos.xy)].g;
}
