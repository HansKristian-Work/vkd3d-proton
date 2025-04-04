RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex, uint gid : SV_GroupID)
{
	WMMA_Type TypeA = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type TypeB = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_B, WaveMatrixShape_16X16, false);
	WMMA_Type TypeC = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);
	WMMA_Type TypeC16 = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);

	WMMA_Matrix A = WMMA_Load(TypeA, Inputs, 0, 32);
	WMMA_Matrix B = WMMA_Load(TypeB, Inputs, 512, 32);
	WMMA_Matrix C = WMMA_Load(TypeC, Inputs, 1024, 64);

	C = WMMA_MatMulAcc(WMMA_F32_16X16X16_F16, A, B, C);
	C = WMMA_Convert(TypeC, TypeC16, C);

	WMMA_Store(TypeC16, Outputs, 0, 32, C);
}
