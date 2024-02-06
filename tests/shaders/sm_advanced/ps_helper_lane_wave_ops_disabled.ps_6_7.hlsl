StructuredBuffer<uint> A;

uint extract_quad_ballot(uint4 b)
{
    uint wave_index = WaveGetLaneIndex() & ~3u;
    return (b[wave_index / 32] >> (wave_index & 31)) & 0xf;
}

// The normal QuadAny/All breaks on AMD in this shader for some reason.
bool QuadAny_war(bool v)
{
    return QuadReadLaneAt(v, 0) || QuadReadLaneAt(v, 1) || QuadReadLaneAt(v, 2) || QuadReadLaneAt(v, 3);
}

bool QuadAll_war(bool v)
{
    return QuadReadLaneAt(v, 0) && QuadReadLaneAt(v, 1) && QuadReadLaneAt(v, 2) && QuadReadLaneAt(v, 3);
}

uint PSMain(int2 coord)
{
    uint v = A[coord.y * 2 + coord.x];

    uint lanes = WaveActiveCountBits(true);
    if ((v & 1) == 0)
        discard;
    uint lanes2 = WaveActiveCountBits(true);

    // Helper lanes are only guaranteed to exist
    // if there are quad / derivative instructions.
    // If there is no control flow path where helpers have visible results, they may die.
    uint any_result_wave = WaveActiveAnyTrue(bool(v & 2));
    uint all_result_wave = WaveActiveAllTrue(bool(v & 2));
    uint elected = uint(WaveIsFirstLane());
    uint first = WaveReadLaneFirst(coord.y * 2 + coord.x);
    uint ballot = extract_quad_ballot(WaveActiveBallot(true));

    uint active_sum = WaveActiveSum(coord.y * 2 + coord.x);
    uint prefix_bits = WavePrefixCountBits(true);

    // Defer quad ops to last section of shader to guarantee
    // that helpers remain alive and participate here.
    //uint any_result = uint(QuadAny(bool(v & 2)));
    //uint all_result = uint(QuadAll(bool(v & 2)));
    uint any_result = uint(QuadAny_war(bool(v & 2)));
    uint all_result = uint(QuadAll_war(bool(v & 2)));

    return lanes | (lanes2 << 3) |
        (any_result << 6) | (all_result << 7) |
        (any_result_wave << 8) | (all_result_wave << 9) |
        (elected << 10) | (first << 11) | (ballot << 13) |
        (active_sum << 17) |
        (prefix_bits << 21);
}

uint main(float4 pos : SV_Position) : SV_Target
{
    return PSMain(int2(pos.xy));
}
