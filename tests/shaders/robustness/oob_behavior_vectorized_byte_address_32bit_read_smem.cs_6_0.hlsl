RWByteAddressBuffer Writeback : register(u1);

RWByteAddressBuffer WriteUint1 : register(u2);
RWByteAddressBuffer WriteUint2 : register(u3);
RWByteAddressBuffer WriteUint3 : register(u4);
RWByteAddressBuffer WriteUint4 : register(u5);

[numthreads(1, 1, 1)]
void main(uint thr : SV_GroupID)
{
	if (4 * thr < 48)
		Writeback.Store<uint>(8 * 64 + 4 * thr, WriteUint1.Load<uint>(4 * thr));

	if (8 * thr < 48)
		Writeback.Store<uint2>(9 * 64 + 8 * thr, WriteUint2.Load<uint2>(8 * thr));

	if (12 * thr < 48)
		Writeback.Store<uint3>(10 * 64 + 12 * thr, WriteUint3.Load<uint3>(12 * thr));

	if (16 * thr < 48)
		Writeback.Store<uint4>(11 * 64 + 16 * thr, WriteUint4.Load<uint4>(16 * thr));
}

