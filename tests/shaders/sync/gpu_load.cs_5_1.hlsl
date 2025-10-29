RWStructuredBuffer<uint> RWData[] : register(u0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_GroupIndex, uint2 gid : SV_GroupID)
{
	// It's hammer time!
	uint orig;
	for (int i = 0; i < 1024; i++)
		InterlockedCompareExchange(RWData[(gid.y << 14) | gid.x][thr], i, (gid.y << 22) | (gid.x << 8) | thr, orig);
}
