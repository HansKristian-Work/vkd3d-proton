RWStructuredBuffer<uint> RWBuf : register(u0);

[Shader("node")]
[NodeLaunch("thread")]
void EntryNode(
		[MaxRecords(16)] [NodeID("A")] EmptyNodeOutput a,
		[MaxRecords(16)] [NodeArraySize(2)] [NodeID("B")] EmptyNodeOutputArray b,
		[MaxRecords(16)] [NodeArraySize(4)] [AllowSparseNodes] [NodeID("C")] EmptyNodeOutputArray c,
		[MaxRecords(16)] [UnboundedSparseNodes] [AllowSparseNodes] [NodeID("D")] EmptyNodeOutputArray d)
{
	a.ThreadIncrementOutputCount(5);
	b[0].ThreadIncrementOutputCount(4);
	b[1].ThreadIncrementOutputCount(6);
	c[1].ThreadIncrementOutputCount(3);
	c[3].ThreadIncrementOutputCount(2);
	d[7].ThreadIncrementOutputCount(9);
	d[8].ThreadIncrementOutputCount(7);
}

[Shader("node")]
[NodeLaunch("thread")]
void A()
{
	uint o;
	InterlockedAdd(RWBuf[0], 1, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("B", 0)]
void B0()
{
	uint o;
	InterlockedAdd(RWBuf[1], 2, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("B", 1)]
void B1()
{
	uint o;
	InterlockedAdd(RWBuf[2], 4, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("C", 1)]
void C1()
{
	uint o;
	InterlockedAdd(RWBuf[3], 8, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("C", 3)]
void C3()
{
	uint o;
	InterlockedAdd(RWBuf[4], 16, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("D", 7)]
void D7()
{
	uint o;
	InterlockedAdd(RWBuf[5], 32, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("D", 8)]
void D8()
{
	uint o;
	InterlockedAdd(RWBuf[6], 64, o);
}

