struct ps_data
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float3 color1 : COLOR1;
    float color2 : COLOR2;
};

ps_data main(float4 position : POSITION)
{
    ps_data o;
    o.position = position;
    o.color = float4(0, 1, 0, 1);
    o.color1 = (float3)0.5;
    o.color2 = 0.25;
    return o;
}
