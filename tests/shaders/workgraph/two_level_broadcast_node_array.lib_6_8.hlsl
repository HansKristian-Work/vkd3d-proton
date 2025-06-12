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
		[MaxRecords(8)] [NodeArraySize(2)] [NodeID("B")] NodeOutputArray<Payload> opayload)
{
	GroupNodeOutputRecords<Payload> write = opayload[entry.Get().node_idx].GetGroupNodeOutputRecords(8);
	write.Get(local_id).grid = entry.Get().size;
	write.Get(local_id).offset = entry.Get().offset + thr * 8 * entry.Get().size;
	write.Get(local_id).increment = entry.Get().increment;
	write.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(8, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
[NodeID("B", 0)]
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
[NodeID("B", 1)]
void Broadcast1(DispatchNodeInputRecord<Payload> payload, uint thr : SV_DispatchThreadID)
{
	Payload p = payload.Get();
	uint o;
	InterlockedAdd(RWBuf1[p.offset + thr], p.increment, o);
}

