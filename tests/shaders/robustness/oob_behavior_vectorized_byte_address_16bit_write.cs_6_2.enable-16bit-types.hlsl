RWByteAddressBuffer WriteShort1 : register(u6);
RWByteAddressBuffer WriteShort2 : register(u7);
RWByteAddressBuffer WriteShort3 : register(u8);
RWByteAddressBuffer WriteShort4 : register(u9);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	WriteShort1.Store<uint16_t>(2 * thr, uint16_t(thr));
	WriteShort2.Store<uint16_t2>(4 * thr, uint16_t(2 * thr) + uint16_t2(0, 1));
	WriteShort3.Store<uint16_t3>(6 * thr, uint16_t(3 * thr) + uint16_t3(0, 1, 2));
	WriteShort4.Store<uint16_t4>(8 * thr, uint16_t(4 * thr) + uint16_t4(0, 1, 2, 3));
}

