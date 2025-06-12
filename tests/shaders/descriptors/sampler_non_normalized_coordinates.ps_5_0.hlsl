SamplerState s : register(s0);
Texture2D<float> t : register(t0);

float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
        return t.Sample(s, pos.xy).xxxx;
}