float4 main(in uint id : SV_VERTEXID) : SV_POSITION
{
    float2 coords = float2((id << 1) & 2, id & 2);
    return float4(coords * float2(2, -2) + float2(-1, 1), 0.5f + dot(coords, float2(0.15f, 0.25f)), 1);
}
