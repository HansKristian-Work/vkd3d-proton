RWStructuredBuffer<uint> Bufs[32] : register(u0, space0);
RWByteAddressBuffer RawBufs[32] : register(u32, space0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	if (thr < 32)
	{
		Bufs[NonUniformResourceIndex(thr)][0] = 4 * thr;
		Bufs[NonUniformResourceIndex(thr)][1] = 4 * thr + 1;
		Bufs[NonUniformResourceIndex(thr)][2] = 4 * thr + 2;
		Bufs[NonUniformResourceIndex(thr)][3] = 4 * thr + 3;
	}
	else
	{
		RawBufs[NonUniformResourceIndex(thr - 32)].Store4(0, 4 * thr + uint4(0, 1, 2, 3));
	}
}

