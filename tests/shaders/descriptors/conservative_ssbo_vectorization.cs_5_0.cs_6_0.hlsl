RWStructuredBuffer<uint4> Outputs : register(u0);

struct Data
{
	uint4 a;
	uint b;
};
StructuredBuffer<Data> Inputs : register(t0);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	uint4 v0 = Inputs[0].a;
	uint4 v1 = Inputs[1].a;
	uint4 v2 = Inputs[4].a;
	uint4 v3 = Inputs[4 * thr].a;

	Outputs[4 * thr + 0] = v0;
	Outputs[4 * thr + 1] = v1;
	Outputs[4 * thr + 2] = v2;
	Outputs[4 * thr + 3] = v3;
}
