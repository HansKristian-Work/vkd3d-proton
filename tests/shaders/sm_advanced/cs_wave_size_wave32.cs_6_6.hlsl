StructuredBuffer<uint> RO : register(t0);
RWStructuredBuffer<uint> RW : register(u0);

void run(uint thr)
{
    RW[thr] = WavePrefixSum(RO[thr]);
    RW[thr + 128] = WaveGetLaneCount();
}

[WaveSize(32)]
[numthreads(128, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    run(thr);
}
