StructuredBuffer<float> Buf : register(t0);
RWStructuredBuffer<uint> RWBuf : register(u0);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    RWBuf[thr] = asuint(f16tof32(uint(Buf[thr])));
}
