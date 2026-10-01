cbuffer RootDesc : register(b1)
{
	float4 data[64];
};

RWStructuredBuffer<float4> RWBuf : register(u0);

cbuffer Cond
{
	uint cond;
};

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID, uint2 gid : SV_GroupID)
{
	float4 value = float4(1, 2, 3, 4);

	// Bait the compiler into optimizing the loads once.
	// Block the compiler from fusing the condition.
	if (cond & 1)
		value += data[63];

	if (value.x > 10.0 && (cond & 4))
		value += data[63];

	if (value.x > 30.0 && (cond & 16))
		value += data[63];

	// Try to bait out any optimization for dynamic indices too.
	if (value.x > 40.0 && (cond & 32))
		value += data[gid.y];
	if (value.x > 50.0 && (cond & 64))
		value += data[gid.y];

	RWBuf[thr] = value;
}
