RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	WMMA_Type Type8A = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type Type8B = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_B, WaveMatrixShape_16X16, true);
	WMMA_Type Type8C = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);
	WMMA_Type TypeC32 = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);

	WMMA_Matrix c = WMMA_MatrixFill(TypeC32, 0);

	// Implement X * Y * Z

	WMMA_Matrix x = WMMA_Load(Type8A, Inputs, 0, 16);
	WMMA_Matrix y = WMMA_Load(Type8A, Inputs, 256, 16);
	WMMA_Matrix z = WMMA_Load(Type8B, Inputs, 512, 16);
	WMMA_Matrix yz32 = WMMA_MatMulAcc(WMMA_F32_16X16X16_FP8_FP8, y, z, c);
	WMMA_Matrix yz = WMMA_ConvertSaturate(TypeC32, Type8B, yz32);
	WMMA_Matrix res = WMMA_MatMulAcc(WMMA_F32_16X16X16_FP8_FP8, x, yz, c);
	res = WMMA_ConvertSaturate(TypeC32, Type8C, res);

	WMMA_Store(Type8C, Outputs, 0, 16, res);
}
