RWStructuredBuffer<uint> RWBuf0 : register(u0);

struct SharedPayload
{
	uint grid : SV_DispatchGrid;
	uint num_atomics;
};

[Shader("node")]
[NodeLaunch("thread")]
void EntryNode([MaxRecords(1)] [NodeID("Broadcast")] NodeOutput<SharedPayload> A)
{
	ThreadNodeOutputRecords<SharedPayload> write0 = A.GetThreadNodeOutputRecords(1);
	write0.Get().grid = 512;
	write0.Get().num_atomics = 9;
	write0.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void Broadcast(DispatchNodeInputRecord<SharedPayload> payload, uint thr : SV_GroupIndex)
{
	uint o;
	InterlockedAdd(RWBuf0[thr], payload.Get().num_atomics, o);
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeDispatchGrid(256, 1, 1)]
[NodeShareInputOf("Broadcast")] 
void TappedInput(DispatchNodeInputRecord<SharedPayload> payload, uint thr : SV_GroupIndex)
{
	uint o;
	InterlockedAdd(RWBuf0[64 + thr], payload.Get().num_atomics, o);
}


