[Shader("node")]
[NodeLaunch("thread")]
void EntryNode([MaxRecords(64)] EmptyNodeOutput A,
		[MaxRecords(64)] EmptyNodeOutput B)
{
	A.ThreadIncrementOutputCount(30);
	B.ThreadIncrementOutputCount(60);
}

RWStructuredBuffer<uint> U0 : register(u0, space1);
RWStructuredBuffer<uint> U1 : register(u1, space1);
cbuffer Cbuf0 : register(b0) { uint Index; };
cbuffer Cbuf1 : register(b1) { uint Count; };

LocalRootSignature LRS0 = { "UAV(u0, space = 1), RootConstants(num32BitConstants = 1, b0), RootConstants(num32BitConstants = 1, b1)" };
LocalRootSignature LRS1 = { "UAV(u1, space = 1), RootConstants(num32BitConstants = 1, b1), RootConstants(num32BitConstants = 1, b0)" };
SubobjectToExportsAssociation assoc1 = { "LRS0", "A" };
SubobjectToExportsAssociation assoc2 = { "LRS1", "B" };

[Shader("node")]
[NodeLaunch("thread")]
[NodeLocalRootArgumentsTableIndex(0)]
void A()
{
	uint o;
	InterlockedAdd(U0[Index], Count, o);
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeLocalRootArgumentsTableIndex(1)]
void B()
{
	uint o;
	InterlockedAdd(U1[Index], Count, o);
}

