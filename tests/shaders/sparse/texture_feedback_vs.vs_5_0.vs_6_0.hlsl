void main(uint idx : SV_VERTEXID, out float4 pos : SV_POSITION, out float2 uv : UV_TEXCOORD)
{
    uv = float2((idx << 1) & 2, idx & 2);
    pos = float4(2.0f * uv.x - 1.0f, 2.0f * (1.0f - uv.y) - 1.0f, 0.0f, 1.0f);
}
