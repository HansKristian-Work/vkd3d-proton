RWStructuredBuffer<uint4> RWResults : register(u0);
StructuredBuffer<float16_t4> Inputs : register(t0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	float16_t4 inp = Inputs[thr];
	uint4 inf = uint4(isinf(inp));
	uint4 nan = uint4(isnan(inp));
	uint4 norm = uint4(isnormal(inp));
	RWResults[3 * thr + 0] = inf;
	RWResults[3 * thr + 1] = nan;
	RWResults[3 * thr + 2] = norm;
}
