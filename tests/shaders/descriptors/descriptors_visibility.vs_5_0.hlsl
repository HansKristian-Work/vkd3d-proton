ByteAddressBuffer b;
Texture2D t;
SamplerState s;

float4 cb;

float4 main(uint id : SV_VertexID) : SV_Position
{
    float2 coords = float2((id << 1) & 2, id & 2);
    uint i;

    if (cb.x != 4.0 || cb.y != 8.0 || cb.z != 16.0 || cb.w != 32.0)
        return (float4)0;

    for (i = 0; i <= 6; ++i)
    {
        if (b.Load(4 * i) != i)
            return (float4)0;
    }

    if (any(t.SampleLevel(s, (float2)0, 0) != float4(1.0, 1.0, 0.0, 1.0)))
        return (float4)0;

    return float4(coords * float2(2, -2) + float2(-1, 1), 0, 1);
}