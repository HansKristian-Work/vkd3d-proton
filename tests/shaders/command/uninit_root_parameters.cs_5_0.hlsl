Buffer<float4> T : register(t0);
RWStructuredBuffer<float4> U : register(u0);
cbuffer C : register(b0)
{
        float4 v;
};

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        U[thr] = T.Load(thr) + v;
}