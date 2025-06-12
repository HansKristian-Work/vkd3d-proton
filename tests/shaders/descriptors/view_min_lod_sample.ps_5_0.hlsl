Texture2D tex;
SamplerState s;
float testLod;

float4 main() : SV_Target
{
    return tex.SampleLevel(s, float2(0, 0), testLod);
}