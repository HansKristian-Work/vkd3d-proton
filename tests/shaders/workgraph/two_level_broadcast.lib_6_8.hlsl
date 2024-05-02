RWStructuredBuffer<uint> RWBuf0 : register(u0);
RWStructuredBuffer<uint> RWBuf1 : register(u1);

struct Payload
{
	uint grid : SV_DispatchGrid;
	uint offset;
	uint increment;
};

struct EntryData
{
	uint grid : SV_DispatchGrid;
	uint node_idx;
	uint size;
	uint offset;
	uint increment;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(8, 1, 1)]
[NodeMaxDispatchGrid(256, 1, 1)]
void EntryNode(DispatchNodeInputRecord<EntryData> entry,
		uint thr : SV_DispatchThreadID,
		uint local_id : SV_GroupIndex,
		[MaxRecords(8)] [NodeID("Broadcast0")] NodeOutput<Payload> opayload0,
		[MaxRecords(8)] [NodeID("Broadcast1")] NodeOutput<Payload> opayload1)
{
	if (entry.Get().node_idx != 0)
	{
		GroupNodeOutputRecords<Payload> write1 = opayload1.GetGroupNodeOutputRecords(8);
		write1.Get(local_id).grid = entry.Get().size;
		write1.Get(local_id).offset = entry.Get().offset + thr * 8 * entry.Get().size;
		write1.Get(local_id).increment = entry.Get().increment;
		write1.OutputComplete();
	}
	else
	{
		ThreadNodeOutputRecords<Payload> write0 = opayload0.GetThreadNodeOutputRecords(1);
		write0.Get().grid = entry.Get().size;
		write0.Get().offset = entry.Get().offset + thr * 8 * entry.Get().size;
		write0.Get().increment = entry.Get().increment;
		write0.OutputComplete();
	}
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(8, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void Broadcast0(DispatchNodeInputRecord<Payload> payload, uint thr : SV_DispatchThreadID)
{
	Payload p = payload.Get();
	uint o;
	InterlockedAdd(RWBuf0[p.offset + thr], p.increment, o);
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(8, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void Broadcast1(DispatchNodeInputRecord<Payload> payload, uint thr : SV_DispatchThreadID)
{
	Payload p = payload.Get();
	uint o;
	InterlockedAdd(RWBuf1[p.offset + thr], p.increment, o);
}

