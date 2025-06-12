StructuredBuffer<uint64_t> RO : register(t0);
RWStructuredBuffer<uint64_t> RW : register(u0);

groupshared uint64_t uv[11];
groupshared int64_t sv[11];

[numthreads(11, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        uv[thr] = thr == 1 || thr == 4 || thr == 7 ? uint64_t(-1) : uint64_t(0);
        sv[thr] = 0;
        GroupMemoryBarrierWithGroupSync();

        if (thr < 4)
        {
                uint64_t v = RO[thr];
                InterlockedAdd(uv[0], v);
                InterlockedAnd(uv[1], ~v);
                InterlockedOr(uv[2], v);
                InterlockedMax(uv[3], v);
                InterlockedMin(uv[4], v);

                InterlockedMax(sv[5], v);
                InterlockedMin(sv[6], v);

                InterlockedXor(uv[7], v);
                uint64_t old_value;
                InterlockedExchange(uv[8], v, old_value);
                InterlockedCompareStore(uv[9], 0, v);
                InterlockedCompareStore(uv[9], 0, v + 1);
                InterlockedCompareExchange(uv[10], 0, v, old_value);
                InterlockedCompareExchange(uv[10], 0, v + 1, old_value);
        }

        GroupMemoryBarrierWithGroupSync();

        RW[thr] = thr == 5 || thr == 6 ? uint64_t(sv[thr]) : uv[thr];
}
