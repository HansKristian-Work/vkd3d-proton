
RWStructuredBuffer<uint> Inputs : register(u0);
RWStructuredBuffer<uint> Feedback : register(u1);

[numthreads(1024, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	uint input = Inputs[thr];
	if (input != 0)
	{
		uint o;
		InterlockedAdd(Feedback[0], 1, o);
	}

	Inputs[thr] = 0xff;
}
