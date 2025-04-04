RWByteAddressBuffer Outputs : register(u0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main()
{
	WMMA_Type TypeA = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type TypeB = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_B, WaveMatrixShape_16X16, false);
	WMMA_Type TypeC = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);
	WMMA_Type TypeA8 = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type TypeB8 = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_B, WaveMatrixShape_16X16, false);

	WMMA_Type TypeAT = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, true);
	WMMA_Type TypeBT = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_B, WaveMatrixShape_16X16, true);
	WMMA_Type TypeCT = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);
	WMMA_Type TypeA8T = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_A, WaveMatrixShape_16X16, true);
	WMMA_Type TypeB8T = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_B, WaveMatrixShape_16X16, true);

	uint size[10];
	size[0] = WMMA_MatrixLength(TypeA);
	size[1] = WMMA_MatrixLength(TypeB);
	size[2] = WMMA_MatrixLength(TypeC);
	size[3] = WMMA_MatrixLength(TypeA8);
	size[4] = WMMA_MatrixLength(TypeB8);
	size[5] = WMMA_MatrixLength(TypeAT);
	size[6] = WMMA_MatrixLength(TypeBT);
	size[7] = WMMA_MatrixLength(TypeCT);
	size[8] = WMMA_MatrixLength(TypeA8T);
	size[9] = WMMA_MatrixLength(TypeB8T);

	for (int i = 0; i < 10; i++)
		Outputs.Store(4 * i, size[i]);
}
