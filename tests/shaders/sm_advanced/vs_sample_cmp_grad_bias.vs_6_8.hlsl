float4 main(uint vid : SV_VertexID, out float2 coord : TEXCOORD) : SV_Position
{
  coord = float2(vid & 2, 2.0f * (vid & 1));
  return float4(2.0f * coord - 1.0f, 0.0f, 1.0f);
}
