Buffer<uint1> SRV0[2] : register(t0);
Buffer<float1> SRV1[2] : register(t2);
Buffer<uint2> SRV2[2] : register(t4);
Buffer<float2> SRV3[2] : register(t6);
Buffer<uint3> SRV4[2] : register(t8);
Buffer<float3> SRV5[2] : register(t10);
Buffer<uint4> SRV6[2] : register(t12);
Buffer<float4> SRV7[2] : register(t14);

RWStructuredBuffer<uint4> UAVStruct[16] : register(u0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	// Access raw buffers as typed. See how it explodes :V
	UAVStruct[0][thr] = SRV0[0].Load(thr).xxxx;
	UAVStruct[1][thr] = SRV0[1].Load(thr).xxxx;
	UAVStruct[2][thr] = asuint(SRV1[0].Load(thr)).xxxx;
	UAVStruct[3][thr] = asuint(SRV1[1].Load(thr)).xxxx;

	UAVStruct[4][thr] = SRV2[0].Load(thr).xyyy;
	UAVStruct[5][thr] = SRV2[1].Load(thr).xyyy;
	UAVStruct[6][thr] = asuint(SRV3[0].Load(thr)).xyyy;
	UAVStruct[7][thr] = asuint(SRV3[1].Load(thr)).xyyy;

	UAVStruct[8][thr] = SRV4[0].Load(thr).xyzz;
	UAVStruct[9][thr] = SRV4[1].Load(thr).xyzz;
	UAVStruct[10][thr] = asuint(SRV5[0].Load(thr)).xyzz;
	UAVStruct[11][thr] = asuint(SRV5[1].Load(thr)).xyzz;

	UAVStruct[12][thr] = SRV6[0].Load(thr);
	UAVStruct[13][thr] = SRV6[1].Load(thr);
	UAVStruct[14][thr] = asuint(SRV7[0].Load(thr));
	UAVStruct[15][thr] = asuint(SRV7[1].Load(thr));
}
