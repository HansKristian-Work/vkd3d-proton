#define rs_text "RootConstants(num32BitConstants=4, b0)"

cbuffer info_t : register(b0)
{
    float4 color;
};

[RootSignature(rs_text)]
float4 main() : SV_TARGET0
{
    return color;
}
