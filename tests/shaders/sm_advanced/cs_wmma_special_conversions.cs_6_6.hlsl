RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	WMMA_Type TypeB = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_B, WaveMatrixShape_16X16, true);
	WMMA_Type TypeC = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);
	WMMA_Matrix m = WMMA_Load(TypeB, Inputs, 0, 16);
	m = WMMA_Convert(TypeB, TypeC, m);
	WMMA_Store(TypeC, Outputs, 0, 64, m);
}
