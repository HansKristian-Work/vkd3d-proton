StructuredBuffer<uint16_t4> RO : register(t0);
RWStructuredBuffer<uint> RW : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    RW[thr] = pack_u8(RO[thr]);
}
