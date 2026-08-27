StructuredBuffer<float4> Buf : register(t0);
RWStructuredBuffer<uint4> RWBuf : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    uint4 a = uint4(Buf[0]) - 1u;
    RWBuf[0] = uint4(a.xy / a.zw, a.xy % a.zw);

    if (a.w != 0u)
        RWBuf[0] = 0xdeadbeef.xxxx;
}
