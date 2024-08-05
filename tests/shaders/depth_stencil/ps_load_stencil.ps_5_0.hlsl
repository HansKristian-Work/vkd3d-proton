Texture2D<uint4> t;

uint4 main(float4 position : SV_Position) : SV_Target
{
    return t[int2(position.x, position.y)];
}
