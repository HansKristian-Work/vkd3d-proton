RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	WMMA_Type TypeB = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_B, WaveMatrixShape_16X16, true);
	WMMA_Type TypeC = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);

	uint len_b = WMMA_MatrixLength(TypeB);
	uint len = WMMA_MatrixLength(TypeC);
	WMMA_Matrix m = WMMA_Load(TypeC, Inputs, 64, 64);

	for (int i = 0; i < len; i++)
	{
		float v = asfloat(WMMA_MatrixElementExtract(TypeC, m, i));
		v += Inputs.Load<float>(4 * i);
		m = WMMA_MatrixElementFill(TypeC, m, i, asuint(v));
	}

	// To detect if we're on RDNA3, which will fail this test.
	Outputs.Store(0, len_b);

	WMMA_Store(TypeC, Outputs, 64, 64, m);
}
