RWStructuredBuffer<uint> RWBuf0 : register(u0);
RWStructuredBuffer<uint> RWBuf1 : register(u1);

struct Payload
{
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
		uint wgx : SV_GroupID,
		uint local_id : SV_GroupIndex,
		[MaxRecords(256)] [NodeID("Thread0")] NodeOutput<Payload> opayload0,
		[MaxRecordsSharedWith(opayload0)] [NodeID("Thread1")] NodeOutput<Payload> opayload1)
{
	if (entry.Get().node_idx != 0)
	{
		GroupNodeOutputRecords<Payload> write1 = opayload1.GetGroupNodeOutputRecords(64 * entry.Get().size);
		for (int i = 0; i < 8 * entry.Get().size; i++)
		{
			write1[local_id + i * 8].offset = entry.Get().offset + (i * 8 + local_id) + 64 * wgx * entry.Get().size;
			write1[local_id + i * 8].increment = entry.Get().increment;
		}
		write1.OutputComplete();
	}
	else
	{
		ThreadNodeOutputRecords<Payload> write0 = opayload0.GetThreadNodeOutputRecords(8 * entry.Get().size);
		for (int i = 0; i < 8 * entry.Get().size; i++)
		{
			write0[i].offset = entry.Get().offset + i + thr * 8 * entry.Get().size;
			write0[i].increment = entry.Get().increment;
		}
		write0.OutputComplete();
	}
}

[Shader("node")]
[NodeLaunch("thread")]
void Thread0(ThreadNodeInputRecord<Payload> payload)
{
	Payload p = payload.Get();
	uint o;
	InterlockedAdd(RWBuf0[p.offset], p.increment, o);
}

[Shader("node")]
[NodeLaunch("thread")]
void Thread1(ThreadNodeInputRecord<Payload> payload)
{
	Payload p = payload.Get();
	uint o;
	InterlockedAdd(RWBuf1[p.offset], p.increment, o);
}

