struct ps_data
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float3 color1 : COLOR1;
    float color2 : COLOR2;
};

float4 main(ps_data i) : SV_Target
{
    return float4(i.color.rgb, i.color2);
}
