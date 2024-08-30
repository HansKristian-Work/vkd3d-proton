RWStructuredBuffer<uint> RWBuf : register(u1);
[numthreads(1, 1, 1)]
void main()
{
        uint v;
        InterlockedAdd(RWBuf[0], 1, v);
}