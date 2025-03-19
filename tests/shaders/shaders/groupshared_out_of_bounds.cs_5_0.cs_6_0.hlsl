StructuredBuffer<int2> IndexPairs : register(t0);
RWStructuredBuffer<int> Outputs : register(u0);

// Try to expose overflow within a spill region or something.
groupshared int local_array[8];
groupshared int local_array2[8];

[numthreads(8, 1, 1)]
void main(uint gid : SV_DispatchThreadID)
{
	local_array[gid] = 0;
	local_array2[gid] = 0;
	GroupMemoryBarrierWithGroupSync();
	int i, o;

	[loop]
	for (i = 0; i < 16; i++)
	{
		int2 pair = IndexPairs[16 * gid + i];
		InterlockedAdd(local_array[pair.x], pair.y, o);
		InterlockedAdd(local_array2[pair.x - 8], pair.x >= 8 ? pair.y : 0, o);
	}

	GroupMemoryBarrierWithGroupSync();

	Outputs[gid + 0] = local_array[gid];
	Outputs[gid + 8] = local_array2[gid];
}
