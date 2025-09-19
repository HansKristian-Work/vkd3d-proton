ByteAddressBuffer SRVRaw[8] : register(t0);
StructuredBuffer<uint1> SRVStruct1[2] : register(t8);
StructuredBuffer<uint2> SRVStruct2[2] : register(t10);
StructuredBuffer<uint3> SRVStruct3[2] : register(t12);
StructuredBuffer<uint4> SRVStruct4[2] : register(t14);


RWByteAddressBuffer UAVRaw[8] : register(u0);
RWStructuredBuffer<uint1> UAVStruct1[2] : register(u8);
RWStructuredBuffer<uint2> UAVStruct2[2] : register(u10);
RWStructuredBuffer<uint3> UAVStruct3[2] : register(u12);
RWStructuredBuffer<uint4> UAVStruct4[2] : register(u14);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	// Access a structured descriptor as BAB.
	UAVRaw[0].Store(4 * thr, SRVRaw[0].Load(4 * thr) | (thr << 24));
	UAVRaw[1].Store(4 * thr, SRVRaw[1].Load(4 * thr) | (thr << 24));
	UAVRaw[2].Store2(8 * thr, SRVRaw[2].Load2(8 * thr) | (thr << 24));
	UAVRaw[3].Store2(8 * thr, SRVRaw[3].Load2(8 * thr) | (thr << 24));
	UAVRaw[4].Store3(12 * thr, SRVRaw[4].Load3(12 * thr) | (thr << 24));
	UAVRaw[5].Store3(12 * thr, SRVRaw[5].Load3(12 * thr) | (thr << 24));
	UAVRaw[6].Store4(16 * thr, SRVRaw[6].Load4(16 * thr) | (thr << 24));
	UAVRaw[7].Store4(16 * thr, SRVRaw[7].Load4(16 * thr) | (thr << 24));

	// Access a raw descriptor as structured.
	UAVStruct1[0][thr] = SRVStruct1[0][thr] | (thr << 24);
	UAVStruct1[1][thr] = SRVStruct1[1][thr] | (thr << 24);
	UAVStruct2[0][thr] = SRVStruct2[0][thr] | (thr << 24);
	UAVStruct2[1][thr] = SRVStruct2[1][thr] | (thr << 24);
	UAVStruct3[0][thr] = SRVStruct3[0][thr] | (thr << 24);
	UAVStruct3[1][thr] = SRVStruct3[1][thr] | (thr << 24);
	UAVStruct4[0][thr] = SRVStruct4[0][thr] | (thr << 24);
	UAVStruct4[1][thr] = SRVStruct4[1][thr] | (thr << 24);
}
