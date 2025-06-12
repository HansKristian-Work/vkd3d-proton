cbuffer Cbuf : register(b0)
{
    uint4 values_root[8];
};

cbuffer Cbuf : register(b0, space1)
{
    uint4 values_table[8];
};

RWStructuredBuffer<uint> RWBuf : register(u0);

uint pack4(uint4 v)
{
    return v.x | (v.y << 8) | (v.z << 16) | (v.w << 24);
}

[numthreads(8, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    uint4 lo = values_root[thr];
    uint4 hi = values_table[thr];
    RWBuf[2 * thr + 0] = pack4(lo);
    RWBuf[2 * thr + 1] = pack4(hi);
}
