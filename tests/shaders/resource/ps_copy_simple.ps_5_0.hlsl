Texture2D<float4> tex : register(t0);

float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
    return tex.Load(int3(pos.xy, 0));
}
