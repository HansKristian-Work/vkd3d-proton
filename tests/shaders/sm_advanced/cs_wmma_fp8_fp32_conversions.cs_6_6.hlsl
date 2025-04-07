RWByteAddressBuffer Outputs : register(u0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex, uint gid : SV_GroupID)
{
	WMMA_Type Type8 = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type Type16 = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type Type16C = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);
	WMMA_Type Type32 = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);

	WMMA_Matrix m = WMMA_MatrixFill(Type8, gid);
	m = WMMA_Convert(Type8, Type16, m);

	WMMA_StoreLDS(Type16, 0, 32, m);
	GroupMemoryBarrierWithGroupSync();
	m = WMMA_LoadLDS(Type16C, 0, 32);
	m = WMMA_Convert(Type16C, Type32, m);

	uint extracted = WMMA_MatrixElementExtract(Type32, m, 0);
	if (thr == 0)
		Outputs.Store(4 * gid, extracted);
}
