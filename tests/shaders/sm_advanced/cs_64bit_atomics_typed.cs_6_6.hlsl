StructuredBuffer<uint64_t> RO : register(t0);
RWTexture2D<uint64_t> RW : register(u0);
RWBuffer<int64_t> RWSigned : register(u1);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        if (thr == 0)
        {
                RW[int2(1, 1)] = uint64_t(-1);
                RW[int2(4, 4)] = uint64_t(-1);
                RW[int2(7, 7)] = uint64_t(-1);
        }
        AllMemoryBarrierWithGroupSync();

        uint64_t v = RO[thr];
        InterlockedAdd(RW[int2(0, 0)], v);
        InterlockedAnd(RW[int2(1, 1)], ~v);
        InterlockedOr(RW[int2(2, 2)], v);
        InterlockedMax(RW[int2(3, 3)], v);
        InterlockedMin(RW[int2(4, 4)], v);
        InterlockedMax(RWSigned[5], v);
        InterlockedMin(RWSigned[6], v);
        InterlockedXor(RW[int2(7, 7)], v);
        uint64_t old_value;
        InterlockedExchange(RW[int2(8, 8)], v, old_value);
        InterlockedCompareStore(RW[int2(9, 9)], 0, v);
        InterlockedCompareStore(RW[int2(9, 9)], 0, v + 1);

        InterlockedCompareExchange(RW[int2(10, 10)], 0, v, old_value);
        InterlockedCompareExchange(RW[int2(10, 10)], 0, v + 1, old_value);
}
