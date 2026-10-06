StructuredBuffer<uint> Inputs : register(t0);
RWStructuredBuffer<uint> Outputs : register(u0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Outputs[thr] = Inputs[thr];
}
