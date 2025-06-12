GlobalRootSignature grs = { "UAV(u0)" };

RWBuffer<float4> U : register(u0);

[shader("raygeneration")]
void main()
{
        U[0] = float4(1, 2, 3, 4);
}