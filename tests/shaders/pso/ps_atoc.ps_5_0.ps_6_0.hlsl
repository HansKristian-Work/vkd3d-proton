cbuffer constants : register(b0)
{
    float alpha;
};

float4 main(in uint sample_mask : SV_COVERAGE) : SV_TARGET
{
    float alpha_out = (sample_mask & 0xff) != 0 ? alpha : 0.0f;
    return float4(1.0f, 1.0f, 1.0f, alpha_out);
}
