RWStructuredBuffer<uint4> Output : register(u0);
StructuredBuffer<uint> RAWBuf : register(t0);
Buffer<uint> TBuf : register(t1);

[numthreads(2, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    uint fb0, fb1;
    uint v0 = RAWBuf.Load(16 * 1024 * thr, fb0);
    // Sanity check against typed buffer.
    uint v1 = TBuf.Load(16 * 1024 * thr, fb1);
    uint s0 = CheckAccessFullyMapped(fb0) ? 1 : 0;
    uint s1 = CheckAccessFullyMapped(fb1) ? 1 : 0;
    Output[thr] = uint4(v0, s0, v1, s1);
}
