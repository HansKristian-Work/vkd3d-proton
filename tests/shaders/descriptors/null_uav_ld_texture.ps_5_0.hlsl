RWTexture2D<float> u;

float4 main(float4 position : SV_Position) : SV_Target
{
    float2 s;
    u.GetDimensions(s.x, s.y);
    return u[s * float2(position.x / 640.0f, position.y / 480.0f)];
}