Texture2D<uint> t;

float main(float4 position : SV_Position) : SV_Target
{
    return float(t[int2(position.x, position.y)]);
}
