RWStructuredBuffer<uint> RWBuf[4] : register(u0);

[numthreads(1, 1, 1)]
void main(int wg : SV_GroupID)
{
    RWBuf[wg >> 2][wg & 3] = RWBuf[wg >> 2].IncrementCounter() + 64;
}