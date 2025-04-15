RWStructuredBuffer<uint> Outputs : register(u0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	for (int i = 0; i < 16; i++)
		LDS[16 * thr + i] = 0;
	GroupMemoryBarrierWithGroupSync();

	WMMA_Type TypeA8 = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type TypeC32 = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);

	WMMA_Matrix c = WMMA_MatrixFill(TypeC32, asuint(1.0));

	// Offset and stride is in terms of u32 elements when it's LDS.
	WMMA_StoreLDS(TypeC32, 4, 16, c);
	GroupMemoryBarrierWithGroupSync();

	for (int i = 0; i < 8; i++)
	{
		uint value = LDS[4 + 8 * thr + i];
		Outputs[8 * thr + i] = value;
	}

	GroupMemoryBarrierWithGroupSync();

	for (int i = 0; i < 16; i++)
		LDS[16 * thr + i] = 0;
	GroupMemoryBarrierWithGroupSync();

	WMMA_Matrix a = WMMA_MatrixFill(TypeA8, 0x40);

	// Offset and stride is in terms of u32 elements when it's LDS.
	WMMA_StoreLDS(TypeA8, 4, 4, a);

	GroupMemoryBarrierWithGroupSync();

	for (int i = 0; i < 2; i++)
	{
		uint value = LDS[4 + 2 * thr + i];
		Outputs[256 + 2 * thr + i] = value;
	}
}
