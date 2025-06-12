struct Vec8 { uint16_t4 lo; uint16_t4 hi; };

cbuffer Cbuf : register(b0)
{
    Vec8 values_root[8];
};

cbuffer Cbuf : register(b0, space1)
{
    Vec8 values_table[8];
};

RWStructuredBuffer<uint> RWBuf : register(u0);

uint pack4(uint4 v)
{
    return v.x | (v.y << 8) | (v.z << 16) | (v.w << 24);
}

[numthreads(8, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    Vec8 v_root = values_root[thr];
    Vec8 v_table = values_table[thr];
    uint4 lo = uint4(v_root.lo) + uint4(v_root.hi);
    uint4 hi = uint4(v_table.lo) + uint4(v_table.hi);
    RWBuf[2 * thr + 0] = pack4(lo);
    RWBuf[2 * thr + 1] = pack4(hi);
}
