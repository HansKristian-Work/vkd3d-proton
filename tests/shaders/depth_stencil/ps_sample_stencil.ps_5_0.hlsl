Texture2D<uint4> t : register(t1);

float4 main(float4 position : SV_Position) : SV_Target
{
    float2 s;
    t.GetDimensions(s.x, s.y);
    return t.Load(int3(float3(s.x * position.x / 640.0f, s.y * position.y / 480.0f, 0))).y;
}
