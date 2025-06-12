RWStructuredBuffer<uint> RWBuf : register(u0);

struct Payload
{
	uint idx;
	uint v;
};

[Shader("node")]
[NodeLaunch("thread")]
void ThreadNode(ThreadNodeInputRecord<Payload> payload)
{
	uint idx = payload.Get().idx;
	uint o;
	InterlockedAdd(RWBuf[idx], payload.Get().v, o);
}

