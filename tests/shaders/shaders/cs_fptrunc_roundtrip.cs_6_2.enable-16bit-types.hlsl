StructuredBuffer<float> Buf : register(t0);
RWStructuredBuffer<float> RWBuf : register(u0);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    RWBuf[thr] = half(Buf[thr]);
}
