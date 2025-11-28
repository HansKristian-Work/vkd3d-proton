cbuffer vs_args : register(b0)
{
    float depth;
}

float4 main(in uint vid : SV_VERTEXID) : SV_POSITION
{
    return float4(
        float(vid & 2u) * 2.0f - 1.0f,
        float(vid & 1u) * 4.0f - 1.0f,
        depth, 1.0f);
}
