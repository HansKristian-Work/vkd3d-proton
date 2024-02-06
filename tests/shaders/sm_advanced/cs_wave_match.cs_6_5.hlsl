StructuredBuffer<uint> RO : register(t0);
RWStructuredBuffer<uint> RW : register(u0);

[numthreads(16, 1, 1)]
void main()
{
    RW[WaveGetLaneIndex()] = WaveMatch(RO[WaveGetLaneIndex()]).x;
}
