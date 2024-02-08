Texture2D t;
SamplerState s;

float4 main(float4 position : SV_POSITION) : SV_Target
{
    return t.Sample(s, float2(-0.5f, 1.5f));
}