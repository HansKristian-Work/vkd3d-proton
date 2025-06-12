struct data
{
    float4 position : SV_Position;
    float3 color : COLOR;
    float line_density : LINE_DENSITY;
    float line_detail : LINE_DETAIL;
};

data main(data input)
{
    return input;
}