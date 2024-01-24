GlobalRootSignature grs = { "UAV(u1)" };

RWBuffer<float4> U : register(u0);

[RootSignature("UAV(u0)")]
[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
        U[0] = float4(1, 2, 3, 4);
}