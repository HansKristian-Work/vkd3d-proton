Texture2D t;
SamplerState s;

float4 main(float4 position : SV_POSITION) : SV_Target
{
    float2 p;

    p.x = position.x / 32.0f;
    p.y = position.y / 32.0f;
    return t.Sample(s, p);
}