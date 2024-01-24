RWStructuredBuffer<uint> U : register(u0);
SamplerState S : register(s0);
Texture2D<float> T : register(t0);

void run(uint v)
{
    uint o;
    InterlockedAdd(U[0], v * uint(255.0 * T.SampleLevel(S, 1.25.xx, 0.0) + 0.5), o);
}

[shader("raygeneration")]
void RayGen()
{
    run(1);
}

[shader("raygeneration")]
void RayGenCol()
{
    run(10);
}