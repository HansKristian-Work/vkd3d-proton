RWStructuredBuffer<uint> Buf : register(u0);
cbuffer CBuf : register(b0) { uint clear_value; };

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    Buf[thr] = clear_value;
}
