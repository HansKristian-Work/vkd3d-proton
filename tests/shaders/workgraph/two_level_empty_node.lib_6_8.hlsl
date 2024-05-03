RWStructuredBuffer<uint> RWBuf : register(u0);

[Shader("node")]
[NodeLaunch("thread")]
void EntryNode(
		// Cannot mix launch mode within an array (phew).
		[MaxRecords(16)] [NodeID("A")] EmptyNodeOutput a,
		[MaxRecords(16)] [NodeID("B")] EmptyNodeOutput b,
		[MaxRecords(16)] [NodeArraySize(2)] [NodeID("C")] EmptyNodeOutputArray c,
		[MaxRecords(16)] [NodeArraySize(2)] [NodeID("D")] EmptyNodeOutputArray d,
		[MaxRecords(16)] [NodeArraySize(4)] [AllowSparseNodes] [NodeID("E")] EmptyNodeOutputArray e,
		[MaxRecords(16)] [NodeArraySize(4)] [AllowSparseNodes] [NodeID("F")] EmptyNodeOutputArray f,
		[MaxRecords(16)] [UnboundedSparseNodes] [AllowSparseNodes] [NodeID("G")] EmptyNodeOutputArray g,
		[MaxRecords(16)] [UnboundedSparseNodes] [AllowSparseNodes] [NodeID("H")] EmptyNodeOutputArray h)
{
	a.ThreadIncrementOutputCount(5);
	b.ThreadIncrementOutputCount(5);
	c[0].ThreadIncrementOutputCount(4);
	c[1].ThreadIncrementOutputCount(6);
	d[0].ThreadIncrementOutputCount(4);
	d[1].ThreadIncrementOutputCount(6);
	if (!e[0].IsValid() && e[1].IsValid())
		e[1].ThreadIncrementOutputCount(3);
	if (!e[2].IsValid() && e[3].IsValid())
		e[3].ThreadIncrementOutputCount(2);
	if (!f[0].IsValid() && f[1].IsValid())
		f[1].ThreadIncrementOutputCount(3);
	if (!f[2].IsValid() && f[3].IsValid())
		f[3].ThreadIncrementOutputCount(2);
	if (!g[6].IsValid() && g[7].IsValid())
		g[7].ThreadIncrementOutputCount(9);
	if (!g[6].IsValid() && g[8].IsValid())
		g[8].ThreadIncrementOutputCount(7);
	if (!h[6].IsValid() && h[7].IsValid())
		h[7].ThreadIncrementOutputCount(9);
	if (!h[6].IsValid() && h[8].IsValid())
		h[8].ThreadIncrementOutputCount(7);
}

// Plain node
[Shader("node")]
[NodeLaunch("thread")]
void A()
{
	uint o;
	InterlockedAdd(RWBuf[0], 1, o);
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(4, 1, 1)]
void B(EmptyNodeInput i, uint thr : SV_GroupIndex)
{
	if (thr < i.Count())
	{
		uint o;
		InterlockedAdd(RWBuf[1], 1, o);
	}
}

// Dense array node
[Shader("node")]
[NodeLaunch("thread")]
[NodeID("C", 0)]
void C0()
{
	uint o;
	InterlockedAdd(RWBuf[2], 1, o);
}


[Shader("node")]
[NodeLaunch("thread")]
[NodeID("C", 1)]
void C1()
{
	uint o;
	InterlockedAdd(RWBuf[3], 1, o);
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(4, 1, 1)]
[NodeID("D", 0)]
void D0(EmptyNodeInput i, uint thr : SV_GroupIndex)
{
	if (thr < i.Count())
	{
		uint o;
		InterlockedAdd(RWBuf[4], 1, o);
	}
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(4, 1, 1)]
[NodeID("D", 1)]
void D1(EmptyNodeInput i, uint thr : SV_GroupIndex)
{
	if (thr < i.Count())
	{
		uint o;
		InterlockedAdd(RWBuf[5], 1, o);
	}
}

// Sparse array node
[Shader("node")]
[NodeLaunch("thread")]
[NodeID("E", 1)]
void E0()
{
	uint o;
	InterlockedAdd(RWBuf[6], 1, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("E", 3)]
void E1()
{
	uint o;
	InterlockedAdd(RWBuf[7], 1, o);
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(4, 1, 1)]
[NodeID("F", 1)]
void F0(EmptyNodeInput i, uint thr : SV_GroupIndex)
{
	if (thr < i.Count())
	{
		uint o;
		InterlockedAdd(RWBuf[8], 1, o);
	}
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(4, 1, 1)]
[NodeID("F", 3)]
void F1(EmptyNodeInput i, uint thr : SV_GroupIndex)
{
	if (thr < i.Count())
	{
		uint o;
		InterlockedAdd(RWBuf[9], 1, o);
	}
}

// Unbounded sparse
[Shader("node")]
[NodeLaunch("thread")]
[NodeID("G", 7)]
void G0()
{
	uint o;
	InterlockedAdd(RWBuf[10], 1, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeID("G", 8)]
void G1()
{
	uint o;
	InterlockedAdd(RWBuf[11], 1, o);
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(4, 1, 1)]
[NodeID("H", 7)]
void H0(EmptyNodeInput i, uint thr : SV_GroupIndex)
{
	if (thr < i.Count())
	{
		uint o;
		InterlockedAdd(RWBuf[12], 1, o);
	}
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(4, 1, 1)]
[NodeID("H", 8)]
void H1(EmptyNodeInput i, uint thr : SV_GroupIndex)
{
	if (thr < i.Count())
	{
		uint o;
		InterlockedAdd(RWBuf[13], 1, o);
	}
}


