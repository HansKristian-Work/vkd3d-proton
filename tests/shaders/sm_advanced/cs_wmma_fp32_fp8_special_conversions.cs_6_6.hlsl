RWByteAddressBuffer Outputs : register(u0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);
StructuredBuffer<float> Inputs : register(t0);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex, uint gid : SV_GroupID)
{
	WMMA_Type Type8 = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);
	WMMA_Type Type32 = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, false);

	WMMA_Matrix m = WMMA_MatrixFill(Type32, asuint(Inputs[gid]));
	WMMA_Matrix um = WMMA_Convert(Type32, Type8, m);
	WMMA_Matrix sm = WMMA_ConvertSaturate(Type32, Type8, m);
	uint extracted0 = WMMA_MatrixElementExtract(Type8, um, 0);
	uint extracted1 = WMMA_MatrixElementExtract(Type8, sm, 0);

	if (thr == 0)
		Outputs.Store2(8 * gid, uint2(extracted0, extracted1));
}
