void main(float4 position : Sv_Position,
        float2 texcoord0 : TEXCOORD0, float2 texcoord1 : TEXCOORD1,
        float4 texcoord2 : TEXCOORD2, float3 texcoord3 : TEXCOORD3,
        out float4 target0 : Sv_Target0, out uint4 target1 : SV_Target1)
{
    if (all(position.xy < float2(64, 64)))
        target0 = float4(0, 1, 0, 1);
    else
        target0 = float4(0, 0, 0, 0);

    target1.xyzw = 0;
    target1.y = position.w;
}
