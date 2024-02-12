struct Outputs { float4 col0 : SV_Target0; float4 col1 : SV_Target1; };

Outputs main()
{
    Outputs o;
    o.col0 = float4(1.0, 0.0, 0.0, 1.0);
    o.col1 = 0.5;
    return o;
}