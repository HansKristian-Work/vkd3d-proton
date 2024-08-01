cbuffer constants : register(b0)
{
    float depth;
};

float4 main(in uint vid : SV_VERTEXID) : SV_POSITION
{
    return float4(
      4.0f * float(vid & 1) - 1.0f,
      2.0f * float(vid & 2) - 1.0f,
      depth, 1.0f);
}
