RWStructuredBuffer<uint> RWBuf : register(u0);

struct Payload
{
	uint offset;
};

[Shader("node")]
[NodeLaunch("thread")]
void EntryNode(
	[MaxRecords(2)] [NodeID("Recursive")] NodeOutput<Payload> p)
{
	ThreadNodeOutputRecords<Payload> outputs = p.GetThreadNodeOutputRecords(2);
	outputs[0].offset = 1;
	outputs[1].offset = 2;
	outputs.OutputComplete();
}

[Shader("node")]
[NodeLaunch("thread")]
[NodeMaxRecursionDepth(6)]
void Recursive(ThreadNodeInputRecord<Payload> p,
		[MaxRecords(2)] [NodeID("Recursive")] NodeOutput<Payload> op)
{
	uint o;
	InterlockedAdd(RWBuf[p.Get().offset], GetRemainingRecursionLevels() + 1, o);

	// This is a valid way to check if we can recurse or not.
	if (op.IsValid())
	{
		ThreadNodeOutputRecords<Payload> outputs = op.GetThreadNodeOutputRecords(2);
		outputs[0].offset = p.Get().offset + 1;
		outputs[1].offset = p.Get().offset + 2;
		outputs.OutputComplete();
	}
}
