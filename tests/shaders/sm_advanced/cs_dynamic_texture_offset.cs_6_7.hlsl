struct TestInput
{
    float2 uv; float mip; int2 offsets;
};

Texture2D<float> D : register(t0);
SamplerState S : register(s0);
StructuredBuffer<TestInput> T : register(t0, space1);
RWStructuredBuffer<float> Output : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    TestInput input = T[id];
    float result = D.SampleLevel(S, input.uv, input.mip, input.offsets);
    Output[id] = result;
}
