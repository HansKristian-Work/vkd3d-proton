RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	WMMA_Type TypeA16 = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type TypeA16T = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, true);
	WMMA_Type TypeB16 = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_B, WaveMatrixShape_16X16, false);
	WMMA_Type TypeC32 = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);

	WMMA_Matrix m = WMMA_Load(TypeA16, Inputs, 0, 32);
	WMMA_StoreLDS(TypeA16, 0, 32, m);
	GroupMemoryBarrierWithGroupSync();
	WMMA_Matrix a = WMMA_LoadLDS(TypeA16T, 0, 32);
	WMMA_Matrix b = WMMA_LoadLDS(TypeB16, 0, 32);
	WMMA_Matrix c = WMMA_MatrixFill(TypeC32, 0);
	m = WMMA_MatMulAcc(WMMA_F32_16X16X16_F16, a, b, c);

	WMMA_Store(TypeC32, Outputs, 0, 64, m);
}
