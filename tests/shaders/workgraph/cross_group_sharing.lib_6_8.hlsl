RWStructuredBuffer<uint> RWBuf0 : register(u0);

struct [NodeTrackRWInputSharing] SharedPayload
{
	uint grid : SV_DispatchGrid;
	uint num_atomics;
};

[Shader("node")]
[NodeLaunch("thread")]
void EntryNode(
		[MaxRecords(2)] [NodeArraySize(2)] [NodeID("Broadcast")] NodeOutputArray<SharedPayload> A)
{
	ThreadNodeOutputRecords<SharedPayload> write0 = A[0].GetThreadNodeOutputRecords(1);
	write0.Get().grid = 512;
	write0.Get().num_atomics = 9;
	write0.OutputComplete();

	ThreadNodeOutputRecords<SharedPayload> write1 = A[1].GetThreadNodeOutputRecords(1);
	write1.Get().grid = 512;
	write1.Get().num_atomics = 11;
	write1.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
[NodeID("Broadcast", 0)]
void Broadcast0(globallycoherent RWDispatchNodeInputRecord<SharedPayload> payload, uint thr : SV_GroupIndex)
{
	uint o;
	InterlockedAdd(payload.Get().num_atomics, 1, o);
	Barrier(payload, GROUP_SYNC | GROUP_SCOPE | DEVICE_SCOPE);
	// Spec seems to imply we need to have a barrier outselves. WARP fails if we don't.
	if (payload.FinishedCrossGroupSharing())
	{
		InterlockedAdd(RWBuf0[thr], payload.Get().num_atomics, o);
	}
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeDispatchGrid(256, 1, 1)]
[NodeID("Broadcast", 1)]
void Broadcast1(globallycoherent RWDispatchNodeInputRecord<SharedPayload> payload, uint thr : SV_GroupIndex)
{
	uint o;
	InterlockedAdd(payload.Get().num_atomics, 1, o);
	Barrier(payload, GROUP_SYNC | GROUP_SCOPE | DEVICE_SCOPE);
	// Spec seems to imply we need to have a barrier outselves. WARP fails if we don't.
	if (payload.FinishedCrossGroupSharing())
	{
		InterlockedAdd(RWBuf0[64 + thr], payload.Get().num_atomics, o);
	}
}


