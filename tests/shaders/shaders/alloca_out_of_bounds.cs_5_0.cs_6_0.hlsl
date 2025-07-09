StructuredBuffer<int2> IndexPairs : register(t0);
RWStructuredBuffer<int> Outputs : register(u0);

[numthreads(8, 1, 1)]
void main(uint gid : SV_DispatchThreadID)
{
	// Try to expose overflow within a spill region or something.
	int local_array[8] = (int[8])0;
	int local_array2[8] = (int[8])0;
	int i;

	[loop]
	for (i = 0; i < 16; i++)
	{
		int2 pair = IndexPairs[16 * gid + i];
		local_array[pair.x] += pair.y;
		local_array2[pair.x - 8] += pair.x >= 8 ? pair.y : 0;
	}

	[loop]
	for (i = 0; i < 8; i++)
	{
		Outputs[16 * gid + i + 0] = local_array[i];
		Outputs[16 * gid + i + 8] = local_array2[i];
	}
}
