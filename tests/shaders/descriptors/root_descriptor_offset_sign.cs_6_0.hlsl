RWStructuredBuffer<uint4> RW : register(u0);
StructuredBuffer<uint> R0 : register(t0);
ByteAddressBuffer R1 : register(t1);

[numthreads(1, 1, 1)]
void main()
{
        uint a = R0[-1];       // Negative index
        uint b = R0[1u << 30]; // offset 4 GB. Does it overflow back to 0?
        uint c = R1.Load(-4);  // Negative offset
        uint d = R1.Load(0);
        RW[0] = uint4(a, b, c, d);
}