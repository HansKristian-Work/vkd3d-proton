float4 c0;
float4 c1;

void main(out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float4 o2 : SV_Target2)
{
    o0 = c0;
    o1 = c1;
    o2 = 1.0.xxxx;
}
