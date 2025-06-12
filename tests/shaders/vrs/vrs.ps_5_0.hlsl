void main(in float4 vPos : SV_POSITION, out float4 o0 : SV_Target0)
{
    o0 = float4(ddx(vPos.x) / 255.0, ddy(vPos.y) / 255.0, 0.0, 0.0);
}