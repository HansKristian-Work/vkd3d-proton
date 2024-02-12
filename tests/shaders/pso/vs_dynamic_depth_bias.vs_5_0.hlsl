SamplerState s : register(s0);
Texture2D<float> t : register(t0);

cbuffer params : register(b0)
{
    float depth;
    float slope;
};

float4 main(in uint id : SV_VERTEXID) : SV_POSITION
{
    float2 coords = float2((id << 1) & 2, id & 2);
    return float4(coords * float2(2, -2) + float2(-1, 1), depth + slope * coords.y, 1);
}
