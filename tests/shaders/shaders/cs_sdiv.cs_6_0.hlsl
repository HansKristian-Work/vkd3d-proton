StructuredBuffer<float4> Buf : register(t0);
RWStructuredBuffer<int4> RWBuf : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    int4 a = int4(Buf[0]) - 1;
    RWBuf[0] = int4(a.xy / a.zw, a.xy % a.zw);

    if (a.w != 0)
        RWBuf[0] = 0xdeadbeef.xxxx;
}
