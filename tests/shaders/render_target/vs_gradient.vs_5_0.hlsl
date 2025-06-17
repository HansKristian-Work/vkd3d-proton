void main(uint vid : SV_VertexID, out float4 pos : SV_POSITION, out float2 coord : TEXCOORD0)
{
    pos.x = float(vid & 1) * 4.0 - 1.0;
    pos.y = float(vid & 2) * 2.0 - 1.0;
    pos.z = 0.0;
    pos.w = 1.0;

    coord = pos.xy * 0.5f + 0.5f;
}
