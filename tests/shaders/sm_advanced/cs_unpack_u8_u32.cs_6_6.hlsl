StructuredBuffer<uint> RO : register(t0);
RWStructuredBuffer<uint4> RW : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    RW[thr] = unpack_u8u32(RO[thr]);
}
