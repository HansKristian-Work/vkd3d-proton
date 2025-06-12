struct vs_out
{
    float4 position : SV_POSITION;
    float2 color_xy : COLOR0;
    float2 color_zw : COLOR1;
};

float4 main(struct vs_out i) : SV_TARGET
{
    return float4(i.color_xy.xy, i.color_zw.xy);
}
