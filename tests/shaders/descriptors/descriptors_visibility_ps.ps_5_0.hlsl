ByteAddressBuffer b;
Texture2D t;
SamplerState s;

float4 cb;

float4 main(float4 position : SV_POSITION) : SV_Target
{
    if (cb.x != 1.0 || cb.y != 2.0 || cb.z != 3.0 || cb.w != 4.0)
        return float4(1.0, 0.0, 0.0, 1.0);

    if (b.Load(0) != 2 || b.Load(4) != 4 || b.Load(8) != 8)
        return float4(1.0, 0.0, 0.0, 1.0);

    return t.Sample(s, float2(position.x / 32.0, position.y / 32.0));
}