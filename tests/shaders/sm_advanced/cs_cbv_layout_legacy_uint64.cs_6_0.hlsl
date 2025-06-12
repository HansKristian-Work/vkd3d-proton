cbuffer Cbuf : register(b0)
{
    uint64_t4 values_root[8];
};

cbuffer Cbuf : register(b0, space1)
{
    uint64_t4 values_table[8];
};

RWStructuredBuffer<uint> RWBuf : register(u0);

uint pack4(uint4 v)
{
    return v.x | (v.y << 8) | (v.z << 16) | (v.w << 24);
}

[numthreads(8, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    uint64_t4 v = values_root[thr] + values_table[thr];
    uint4 lo = uint4(v);
    uint4 hi = uint4(v >> 32);
    RWBuf[2 * thr + 0] = pack4(lo);
    RWBuf[2 * thr + 1] = pack4(hi);
}
