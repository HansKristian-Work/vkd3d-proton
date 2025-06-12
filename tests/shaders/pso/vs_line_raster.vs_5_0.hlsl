float4 main(in uint id : SV_VERTEXID) : SV_POSITION
{
    float x = float(id) * 2.0f - 1.0f;
    return float4(x, x * 0.55f, 0.0f, 1.0f);
}
