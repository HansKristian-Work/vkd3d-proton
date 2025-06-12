Texture2D t;
SamplerComparisonState s : register(s1);

float ref;

float4 main(float4 position : SV_Position) : SV_Target
{
    return t.SampleCmp(s, float2(position.x / 640.0f, position.y / 480.0f), ref);
}
