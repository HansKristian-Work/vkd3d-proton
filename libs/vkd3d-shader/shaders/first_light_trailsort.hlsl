// SPDX-License-Identifier: LGPL-2.1-or-later
// Portable replacement for the known First Light TrailSort_CS shader.
// Stable unsigned radix-32 sort, seven passes, exactly one 1024-thread group.
// t0 = input[N], t1[0] = N, u0 = output[2*N] (second half is scratch if N>1024).
// The 32-lane partitions below are logical ranges in shared memory, independent
// of hardware wave width. No Wave intrinsics and no fixed WaveSize requirement.
Buffer<uint> trailsort_input : register(t0);
Buffer<uint> trailsort_count : register(t1);
RWBuffer<uint> trailsort_output : register(u0);

groupshared uint histogram[1024]; // [digit][logical partition]
groupshared uint totals[32];
groupshared uint bases[32];
groupshared uint digits[1024];
groupshared uint pingpong[2048];

// Called uniformly by all threads after histogram writes have completed.
void scan_histogram(uint tid)
{
    if (tid < 32)
    {
        uint sum = 0;
        for (uint p = 0; p < 32; ++p)
        {
            uint index = tid * 32 + p;
            uint count = histogram[index];
            histogram[index] = sum;
            sum += count;
        }
        totals[tid] = sum;
    }
    GroupMemoryBarrierWithGroupSync();
    if (tid < 32)
    {
        uint sum = 0;
        for (uint d = 0; d < tid; ++d)
            sum += totals[d];
        bases[tid] = sum;
    }
    GroupMemoryBarrierWithGroupSync();
}

uint read_large(uint index, uint pass, uint n)
{
    if (pass == 0)
        return trailsort_input[index];
    return trailsort_output[index + ((pass & 1) ? 0 : n)];
}

[numthreads(1024, 1, 1)]
void TrailSort_CS(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint tid = dispatch_id.x;
    uint n = trailsort_count[0];
    uint partition = tid / 32;
    uint lane = tid % 32;
    if (n <= 1024)
    {
        for (uint pass = 0; pass < 7; ++pass)
        {
            uint value = 0;
            if (tid < n)
                value = pass == 0 ? trailsort_input[tid] : pingpong[((pass ^ 1) & 1) * 1024 + tid];
            uint digit = (value >> (pass * 5)) & 31;
            digits[tid] = tid < n ? digit : 32;
            histogram[tid] = 0;
            GroupMemoryBarrierWithGroupSync();
            if (tid < n)
                InterlockedAdd(histogram[digit * 32 + partition], 1);
            GroupMemoryBarrierWithGroupSync();
            scan_histogram(tid);
            if (tid < n)
            {
                uint rank = 0;
                for (uint prev = 0; prev < lane; ++prev)
                    rank += uint(digits[partition * 32 + prev] == digit);
                uint dest = bases[digit] + histogram[digit * 32 + partition] + rank;
                if (pass == 6)
                    trailsort_output[dest] = value;
                else
                    pingpong[(pass & 1) * 1024 + dest] = value;
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
    else
    {
        uint blocks = (n + 1023) / 1024;
        for (uint pass = 0; pass < 7; ++pass)
        {
            histogram[tid] = 0;
            GroupMemoryBarrierWithGroupSync();
            for (uint block = 0; block < blocks; ++block)
            {
                uint index = (partition * blocks + block) * 32 + lane;
                if (index < n)
                {
                    uint digit = (read_large(index, pass, n) >> (pass * 5)) & 31;
                    InterlockedAdd(histogram[digit * 32 + partition], 1);
                }
            }
            GroupMemoryBarrierWithGroupSync();
            scan_histogram(tid);
            for (uint block = 0; block < blocks; ++block)
            {
                uint index = (partition * blocks + block) * 32 + lane;
                uint value = index < n ? read_large(index, pass, n) : 0;
                uint digit = (value >> (pass * 5)) & 31;
                digits[tid] = index < n ? digit : 32;
                GroupMemoryBarrierWithGroupSync();
                if (index < n)
                {
                    uint rank = 0;
                    for (uint prev = 0; prev < lane; ++prev)
                        rank += uint(digits[partition * 32 + prev] == digit);
                    uint dest = bases[digit] + histogram[digit * 32 + partition] + rank;
                    trailsort_output[(pass & 1) * n + dest] = value;
                }
                // Every reader must finish before offsets advance or digits change.
                GroupMemoryBarrierWithGroupSync();
                if (index < n)
                    InterlockedAdd(histogram[digit * 32 + partition], 1);
                GroupMemoryBarrierWithGroupSync();
            }
            // The next pass reads the UAV half written by this pass.
            AllMemoryBarrierWithGroupSync();
        }
    }
}
