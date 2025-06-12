cbuffer color_t : register(b0)
{
    float depth;
    uint3 colors;
    uint sample_mask;
}

struct out_t
{
    float d : SV_Depth;
    float4 a : SV_Target0;
    float4 b : SV_Target1;
    float4 c : SV_Target2;
    uint sm : SV_Coverage;
};

float4 unpack_unorm(uint un)
{
    return float4((un >>  0) & 0xff, (un >>  8) & 0xff,
            (un >> 16) & 0xff, (un >> 24) & 0xff) / 255.0f;
}

out_t main()
{
    out_t result;
    result.d = depth;
    result.a = unpack_unorm(colors.x);
    result.b = unpack_unorm(colors.y);
    result.c = unpack_unorm(colors.z);
    result.sm = sample_mask;
    return result;
}
