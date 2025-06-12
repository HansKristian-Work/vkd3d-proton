Texture2D t;
SamplerState s;

float4 main(float4 position : SV_Position) : SV_Target
{
    return t.Sample(s, float2(position.x / 640.0f, position.y / 480.0f));
}
