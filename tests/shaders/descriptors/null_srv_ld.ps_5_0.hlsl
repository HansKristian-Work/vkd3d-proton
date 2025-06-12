Texture2D t;

uint4 location;

float4 main(float4 position : SV_Position) : SV_Target
{
    return t.Load(location.xyz);
}