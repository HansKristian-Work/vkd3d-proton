float4 main(float4 position : SV_Position,
        float2 texcoord : TEXCOORD) : SV_Target
{
    return float4(position.xy, texcoord);
}