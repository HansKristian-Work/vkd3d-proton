cbuffer constants : register(b0)
{
    uint sample_mask_in;
};

float4 main(out uint sample_mask : SV_COVERAGE) : SV_TARGET
{
    sample_mask = sample_mask_in;
    return float4(1.0f, 1.0f, 1.0f, 0.0f);
}
