void main(float4 in_position : POSITION,
        float4 in_texcoord0 : TEXCOORD0, float4 in_texcoord1 : TEXCOORD1,
        float4 in_texcoord2 : TEXCOORD2,
        out float4 position : Sv_Position,
        out float2 texcoord0 : TEXCOORD0, out float2 texcoord1 : TEXCOORD1,
        out float4 texcoord2 : TEXCOORD2, out float3 texcoord3 : TEXCOORD3)
{
    position = in_position;
    texcoord0 = in_texcoord0.yx;
    texcoord1 = in_texcoord0.wz;
    texcoord2 = in_texcoord1;
    texcoord3 = in_texcoord2.yzx;
}
