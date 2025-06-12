struct Outputs
{
    float4 col0 : SV_TARGET0;
    float col1 : SV_TARGET1;
};

Outputs main()
{
    Outputs o;
    o.col0 = float4(1.0, 0.0, 0.0, 1.0);
    o.col1 = 0.5;
    return o;
}
