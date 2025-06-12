Texture2DMS<float4> tex_ms : register(t0);

uint main(in float4 coord : SV_POSITION) : SV_TARGET
{
    uint count = 0;

    for (uint i = 0; i < 4; i++)
        count += tex_ms.Load(int2(coord.xy), i).x != 0.0f ? 1u : 0u;

    return count;
}
