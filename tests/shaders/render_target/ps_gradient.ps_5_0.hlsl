float4 main(in float4 pos : SV_POSITION, in float2 coord : TEXCOORD0) : SV_TARGET0
{
    return float4(coord.x, coord.y, coord.x + coord.y, coord.x * coord.y);
}
