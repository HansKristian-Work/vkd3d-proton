ByteAddressBuffer SRVRaw[8] : register(t0);
StructuredBuffer<uint1> SRVStruct1[2] : register(t8);
StructuredBuffer<uint2> SRVStruct2[2] : register(t10);
StructuredBuffer<uint3> SRVStruct3[2] : register(t12);
StructuredBuffer<uint4> SRVStruct4[2] : register(t14);

RWStructuredBuffer<uint4> UAVs[16] : register(u0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	UAVs[0][thr] = SRVRaw[0].Load(4 * thr).xxxx;
	UAVs[1][thr] = SRVRaw[1].Load(4 * thr).xxxx;
	UAVs[2][thr] = SRVRaw[2].Load2(8 * thr).xyyy;
	UAVs[3][thr] = SRVRaw[3].Load2(8 * thr).xyyy;
	UAVs[4][thr] = SRVRaw[4].Load3(12 * thr).xyzz;
	UAVs[5][thr] = SRVRaw[5].Load3(12 * thr).xyzz;
	UAVs[6][thr] = SRVRaw[6].Load4(16 * thr);
	UAVs[7][thr] = SRVRaw[7].Load4(16 * thr);

	UAVs[8][thr] = SRVStruct1[0][thr].xxxx;
	UAVs[9][thr] = SRVStruct1[1][thr].xxxx;
	UAVs[10][thr] = SRVStruct2[0][thr].xyyy;
	UAVs[11][thr] = SRVStruct2[1][thr].xyyy;
	UAVs[12][thr] = SRVStruct3[0][thr].xyzz;
	UAVs[13][thr] = SRVStruct3[1][thr].xyzz;
	UAVs[14][thr] = SRVStruct4[0][thr];
	UAVs[15][thr] = SRVStruct4[1][thr];
}
