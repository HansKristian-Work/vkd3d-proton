StructuredBuffer<int> RO : register(t0);
RWStructuredBuffer<int16_t4> RW : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    RW[thr] = unpack_s8s16(RO[thr]);
}
