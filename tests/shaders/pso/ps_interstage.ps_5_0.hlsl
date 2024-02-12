void main(float4 position : SV_Position, float2 t0 : TEXCOORD0,
        nointerpolation float t1 : TEXCOORD1, uint t2 : TEXCOORD2,
        uint t3 : TEXCOORD3, float t4 : TEXCOORD4, out float4 o : SV_Target)
{
    o.x = t0.y + t1;
    o.y = t2 + t3;
    o.z = t4;
    o.w = t0.x;
}
