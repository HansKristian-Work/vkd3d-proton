StructuredBuffer<uint> RO : register(t0);
RWStructuredBuffer<uint16_t4> RW : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    RW[thr] = unpack_u8u16(RO[thr]);
}
