Texture2DMS<float4> t;

uint sample;
uint rt_size;

float4 main(float4 position : SV_Position) : SV_Target
{
    float3 p;
    t.GetDimensions(p.x, p.y, p.z);
    p *= float3(position.x / rt_size, position.y / rt_size, 0);
    return t.Load((int2)p.xy, sample);
}
