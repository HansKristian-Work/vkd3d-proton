StructuredBuffer<uint64_t> RO : register(t0);
RWStructuredBuffer<uint64_t> RW : register(u0);
RWStructuredBuffer<int64_t> RWSigned : register(u1);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        if (thr == 0)
        {
                RW[1] = uint64_t(-1);
                RW[4] = uint64_t(-1);
                RW[7] = uint64_t(-1);
        }
        AllMemoryBarrierWithGroupSync();

        uint64_t v = RO[thr];
        InterlockedAdd(RW[0], v);
        InterlockedAnd(RW[1], ~v);
        InterlockedOr(RW[2], v);
        InterlockedMax(RW[3], v);
        InterlockedMin(RW[4], v);
        InterlockedMax(RWSigned[5], v);
        InterlockedMin(RWSigned[6], v);
        InterlockedXor(RW[7], v);
        uint64_t old_value;
        InterlockedExchange(RW[8], v, old_value);
        InterlockedCompareStore(RW[9], 0, v);
        InterlockedCompareStore(RW[9], 0, v + 1);

        InterlockedCompareExchange(RW[10], 0, v, old_value);
        InterlockedCompareExchange(RW[10], 0, v + 1, old_value);
}
