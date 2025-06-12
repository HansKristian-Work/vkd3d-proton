RWStructuredBuffer<uint> RWBuf : register(u0);

struct Payload
{
	uint v;
	uint size : SV_DispatchGrid;
	uint w;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(3, 1, 1)]
[NumThreads(2, 3, 4)]
void BroadcastNode(DispatchNodeInputRecord<Payload> payload, uint3 thr : SV_DispatchThreadID)
{
	uint idx = thr.z * 100 + thr.y * 10 + thr.x;
	uint o;
	InterlockedAdd(RWBuf[idx], payload.Get().v ^ payload.Get().w, o);
}

