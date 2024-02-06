StructuredBuffer<uint> RO : register(t0);
RWStructuredBuffer<uint> RW : register(u0);

[numthreads(16, 1, 1)]
void main()
{
    uint4 mask = uint4(RO[WaveGetLaneIndex()], 0, 0, 0);
    RW[WaveGetLaneIndex() + 16 * 0] = WaveMultiPrefixSum(WaveGetLaneIndex() + 1, mask);
    RW[WaveGetLaneIndex() + 16 * 1] = WaveMultiPrefixProduct(WaveGetLaneIndex() + 1, mask);
    RW[WaveGetLaneIndex() + 16 * 2] = WaveMultiPrefixCountBits((mask.x & 0xaaaa) != 0, mask);
    RW[WaveGetLaneIndex() + 16 * 3] = WaveMultiPrefixBitAnd(WaveGetLaneIndex() + 1, mask);
    RW[WaveGetLaneIndex() + 16 * 4] = WaveMultiPrefixBitOr(WaveGetLaneIndex() + 1, mask);
    RW[WaveGetLaneIndex() + 16 * 5] = WaveMultiPrefixBitXor(WaveGetLaneIndex() + 1, mask);
}
