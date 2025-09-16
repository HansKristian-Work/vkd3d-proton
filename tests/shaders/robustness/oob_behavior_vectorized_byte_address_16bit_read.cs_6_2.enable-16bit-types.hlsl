RWByteAddressBuffer Writeback : register(u1);

RWByteAddressBuffer WriteShort1 : register(u6);
RWByteAddressBuffer WriteShort2 : register(u7);
RWByteAddressBuffer WriteShort3 : register(u8);
RWByteAddressBuffer WriteShort4 : register(u9);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	if (2 * thr < 48)
		Writeback.Store<uint16_t>(4 * 64 + 2 * thr, WriteShort1.Load<uint16_t>(2 * thr));

	if (4 * thr < 48)
		Writeback.Store<uint16_t2>(5 * 64 + 4 * thr, WriteShort2.Load<uint16_t2>(4 * thr));

	if (6 * thr < 48)
		Writeback.Store<uint16_t3>(6 * 64 + 6 * thr, WriteShort3.Load<uint16_t3>(6 * thr));

	if (8 * thr < 48)
		Writeback.Store<uint16_t4>(7 * 64 + 8 * thr, WriteShort4.Load<uint16_t4>(8 * thr));
}

