Texture2D<float> tex : register(t0);

SamplerComparisonState samp : register(s0);

cbuffer Args : register(b0)
{
  float2 grad;
  float bias;
  float min_lod;
  float dref;
};

float4 main(in float4 position : SV_POSITION, in float2 coord : TEXCOORD) : SV_Target
{
  return float4(
    tex.SampleCmpBias(samp, coord, dref, bias, 0, min_lod),
    tex.SampleCmpGrad(samp, coord, dref, float2(grad.x, 0.0f), float2(0.0f, grad.y), 0, min_lod),
    tex.CalculateLevelOfDetail(samp, coord * exp2(bias)),
    tex.CalculateLevelOfDetailUnclamped(samp, coord * exp2(bias)));
}
