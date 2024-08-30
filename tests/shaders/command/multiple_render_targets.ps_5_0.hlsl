void main(out float4 target0 : SV_Target0, out float4 target1 : SV_Target1,
        out float4 target2 : SV_Target2)
{
    target0 = float4(1.0f, 0.0f, 0.0f, 1.0f);
    target1 = float4(2.0f, 0.0f, 0.0f, 1.0f);
    target2 = float4(3.0f, 0.0f, 0.0f, 1.0f);
}