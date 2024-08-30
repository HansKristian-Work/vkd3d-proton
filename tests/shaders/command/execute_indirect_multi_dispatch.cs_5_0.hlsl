RWStructuredBuffer<uint> RWBuf : register(u0);

[numthreads(1, 1, 1)]
void main()
{
        uint o;
        InterlockedAdd(RWBuf[0], 1, o);
}