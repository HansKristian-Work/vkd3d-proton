RWStructuredBuffer<float4> Outputs : register(u0);

static const float Bs[] = { 101, 102, 103, 104, 105, 106, 107, 108, 109, 0 };
static const float As[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
//static const float Bs[] = { 101, 102, 103, 104, 105, 106, 107, 108, 109 };
//static const float Cs[] = { 201, 202, 203, 204, 205, 206, 207, 208, 209 };
//static const float Ds[] = { 301, 302, 303, 304, 305, 306, 307, 308, 309 };

cbuffer cbuf : register(b0) { uint xor0, xor1; }; // Must make this cbuf, otherwise compiler complains about static OOB.

[numthreads(8, 1, 1)]
void main(uint gid : SV_DispatchThreadID)
{
	// Test various hazard conditions. Negative indices, and very large indices that may overflow with addr computation.
	float b = Bs[min(gid, 9)];
	Outputs[gid] = float4(As[gid], As[gid - 1000], As[gid ^ xor0], As[gid ^ xor1]) + b;
}
