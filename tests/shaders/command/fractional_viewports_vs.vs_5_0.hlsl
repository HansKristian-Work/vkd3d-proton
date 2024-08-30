void main(in float4 in_position : POSITION,
        in float2 in_texcoord : TEXCOORD,
        out float4 position : SV_Position,
        out float2 texcoord : TEXCOORD)
{
    position = in_position;
    texcoord = in_texcoord;
}