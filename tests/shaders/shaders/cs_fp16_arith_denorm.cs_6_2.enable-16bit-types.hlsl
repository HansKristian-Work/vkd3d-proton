StructuredBuffer<float> Buf : register(t0);
RWStructuredBuffer<half2> RWBuf : register(u0);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        precise half result = half(Buf[thr]) - half(Buf[thr ^ 3]);
        RWBuf[thr].x = result;
}
