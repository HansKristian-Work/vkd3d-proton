RWByteAddressBuffer WriteUint1 : register(u2);
RWByteAddressBuffer WriteUint2 : register(u3);
RWByteAddressBuffer WriteUint3 : register(u4);
RWByteAddressBuffer WriteUint4 : register(u5);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	WriteUint1.Store<uint>(4 * thr, thr);
	WriteUint2.Store<uint2>(8 * thr, 2 * thr + uint2(0, 1));
	WriteUint3.Store<uint3>(12 * thr, 3 * thr + uint3(0, 1, 2));
	WriteUint4.Store<uint4>(16 * thr, 4 * thr + uint4(0, 1, 2, 3));
}

