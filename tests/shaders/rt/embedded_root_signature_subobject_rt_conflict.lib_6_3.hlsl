GlobalRootSignature grs = { "UAV(u0)" };
GlobalRootSignature grs2 = { "UAV(u1)" };

RWBuffer<float4> U : register(u0);

[shader("raygeneration")]
void main()
{
        U[0] = float4(1, 2, 3, 4);
}