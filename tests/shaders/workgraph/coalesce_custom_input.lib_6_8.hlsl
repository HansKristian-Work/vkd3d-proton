RWStructuredBuffer<uint> RWBuf : register(u0);

struct Payload
{
	uint idx;
	uint v;
};

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(24, 1, 1)]
void CoalesceNode([MaxRecords(3)] GroupNodeInputRecords<Payload> payload, uint thr : SV_GroupThreadID)
{
	uint count = payload.Count();
	for (uint i = 0; i < count; i++)
	{
		Payload p = payload.Get(i);
		uint o;
		InterlockedAdd(RWBuf[p.idx + thr], p.v, o);
	}
}

