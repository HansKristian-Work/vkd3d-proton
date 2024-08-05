Texture2D<float> t;

float main(float4 position : SV_Position) : SV_Target
{
    return t[int2(position.x, position.y)];
}
