RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	WMMA_Type TypeA8 = WMMA_MakeType(WaveMatrixDataFormat_FP8, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Matrix m = WMMA_Load(TypeA8, Inputs, 0, 16);
	uint len = WMMA_MatrixLength(TypeA8);

	// 8-bit matrices are tightly packed ;_;
	for (uint i = 0; i < len; i++)
	{
		uint elem = WMMA_MatrixElementExtract(TypeA8, m, i);
		Outputs.Store(256 + 4 * (thr * len + i), elem);
		elem ^= 0xff00ff;
		m = WMMA_MatrixElementFill(TypeA8, m, i, elem);
	}

	WMMA_Store(TypeA8, Outputs, 0, 16, m);
}
