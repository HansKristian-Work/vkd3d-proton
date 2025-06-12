#define rs_text "UAV(u0), RootConstants(num32BitConstants=1, b0)"

RWStructuredBuffer<uint> buffer : register(u0);

cbuffer info_t : register(b0)
{
    uint value;
};

[RootSignature(rs_text)]
[numthreads(1,1,1)]
void main()
{
    buffer[0] = value;
}
