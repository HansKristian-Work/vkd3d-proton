StructuredBuffer<float> Buf : register(t0);
RWStructuredBuffer<uint> RWBuf : register(u0);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    precise float added_result = Buf[thr] + 1.0;
    RWBuf[thr] = asuint16(half(added_result));
}
