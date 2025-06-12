RWStructuredBuffer<uint> RWBuf : register(u0);

struct InputRecord
{
	uint3 grid : SV_DispatchGrid;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(2, 2, 2)]
[NumThreads(2, 3, 4)]
void BroadcastNode(DispatchNodeInputRecord<InputRecord> input, uint3 thr : SV_DispatchThreadID)
{
	uint idx = thr.z * 100 + thr.y * 10 + thr.x;
	uint o;
	InterlockedAdd(RWBuf[idx], 1, o);
}

