Texture2D<float> Tex : register(t0);
SamplerState Samp[] : register(s0);
RWByteAddressBuffer OBuffer : register(u0);

[numthreads(64, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
    // Should alternate between wrap (sample 0), or clamp (sample 100).
    uint value = Tex.SampleLevel(Samp[NonUniformResourceIndex(idx)], 1.1.xx, 0.0);
    OBuffer.Store(4 * idx, value);
}