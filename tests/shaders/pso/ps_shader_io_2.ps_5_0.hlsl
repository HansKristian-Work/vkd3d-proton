void main(float4 position : Sv_Position,
        float2 texcoord0 : TEXCOORD0, float2 texcoord1 : TEXCOORD1,
        float4 texcoord2 : TEXCOORD2, float3 texcoord3 : TEXCOORD3,
        out float4 target0 : Sv_Target0, out uint4 target1 : SV_Target1)
{
    target0.x = texcoord0.x + texcoord0.y;
    target0.y = texcoord1.x;
    target0.z = texcoord3.z;
    target0.w = texcoord1.y;

    target1.x = texcoord2.x;
    target1.y = texcoord2.y;
    target1.w = texcoord2.w;
    target1.z = 0;
}
