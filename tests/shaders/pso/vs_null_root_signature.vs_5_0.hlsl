#define rs_text "RootConstants(num32BitConstants=4, b0)"

[RootSignature(rs_text)]
float4 main(uint vid : SV_VERTEXID) : SV_POSITION
{
    return float4(
            -1.0f + 4.0f * float(vid % 2),
            -1.0f + 2.0f * float(vid & 2),
            0.0f, 1.0f);
}
