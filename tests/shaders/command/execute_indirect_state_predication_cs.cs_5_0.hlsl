cbuffer C0 : register(b0)
{
        uint offset;
};

cbuffer C1 : register(b1)
{
        uint multiplier;
};

RWStructuredBuffer<uint> U : register(u0);

[numthreads(1, 1, 1)]
void main()
{
        uint o;
        InterlockedAdd(U[offset], multiplier, o);
}