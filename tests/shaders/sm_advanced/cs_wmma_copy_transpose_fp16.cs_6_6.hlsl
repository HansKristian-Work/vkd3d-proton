RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	// Transpose layout is ignored on arith ops it seems.
	WMMA_Type TypeA = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type TypeB = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_B, WaveMatrixShape_16X16, false);
	WMMA_Type TypeC = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);
	WMMA_Type TypeC32 = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);
	WMMA_Type TypeB8 = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_B, WaveMatrixShape_16X16, true);

	// Tests if we can just YOLO convert to different types.
	// It seems to work ...
	WMMA_Matrix m = WMMA_Load(TypeC, Inputs, 0, 32);
	WMMA_Matrix a = WMMA_Convert(TypeC, TypeA, m);
	WMMA_Matrix b = WMMA_Convert(TypeC, TypeB, m);
	WMMA_Matrix c = WMMA_Convert(TypeC, TypeC32, m);
	m = WMMA_MatMulAcc(WMMA_F32_16X16X16_F16, a, b, c);
	WMMA_Store(TypeC32, Outputs, 0, 64, m);

	// Possible to transpose and do format conversions ... >_<
	// Ran into these in the wild.
	m = WMMA_Convert(TypeC32, TypeB8, m);
	WMMA_Store(TypeB8, Outputs, 1024, 16, m);

	m = WMMA_Convert(TypeB8, TypeC32, m);
	WMMA_Store(TypeC32, Outputs, 2048, 64, m);
}
