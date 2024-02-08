Texture2D t0;
Texture2D t1;
SamplerState s;

float4 main(float4 position : SV_POSITION) : SV_Target
{
    float2 p = float2(position.x / 32.0f, position.x / 32.0f);
    return float4(t0.Sample(s, p).r, t1.Sample(s, p).r, 0, 1);
}
