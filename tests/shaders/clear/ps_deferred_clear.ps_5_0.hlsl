struct ps_out
{
    float4 rt0 : SV_TARGET0;
    float4 rt1 : SV_TARGET1;
    float4 rt2 : SV_TARGET2;
    float4 rt3 : SV_TARGET3;
};

ps_out main()
{
    ps_out result;
    result.rt0 = float4(1.0f, 0.0f, 1.0f, 1.0f);
    result.rt1 = result.rt0;
    result.rt2 = result.rt0;
    result.rt3 = result.rt0;
    return result;
}
