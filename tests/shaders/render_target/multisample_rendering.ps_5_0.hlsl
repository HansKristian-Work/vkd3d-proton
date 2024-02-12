float4 main(uint id : SV_SampleIndex) : SV_Target
{
    switch (id)
    {
        case 0:  return float4(1.0f, 0.0f, 0.0f, 1.0f);
        case 1:  return float4(0.0f, 1.0f, 0.0f, 1.0f);
        case 2:  return float4(0.0f, 0.0f, 1.0f, 1.0f);
        default: return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
}