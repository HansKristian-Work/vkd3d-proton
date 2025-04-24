float4 main(in float4 p : SV_Position, in float4 a : TEXCOORD0, in float4 b : TEXCOORD1, in float4 c : TEXCOORD2) : SV_Target
{
    return float4(a.x, b.y, c.z, 1.0);
}