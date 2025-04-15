RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	WMMA_Matrix as[2];
	WMMA_Matrix bs[2];
	WMMA_Matrix cs[2];
	WMMA_Matrix ds[2];

	WMMA_Type TypeA = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_A, WaveMatrixShape_16X16, false);
	WMMA_Type TypeB = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_B, WaveMatrixShape_16X16, true);
	WMMA_Type TypeC = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);

	[loop]
	for (int i = 0; i < 2; i++)
	{
		as[i] = WMMA_Load(TypeA, Inputs, 32 * i, 64);
		bs[i] = WMMA_Load(TypeB, Inputs, 1024 + 32 * i, 64);
	}

	// Test case where we have static write addressing.
	[unroll]
	for (int i = 0; i < 2; i++)
	{
		cs[1 - i] = WMMA_Load(TypeA, Inputs, 32 - 32 * i, 64);
		ds[1 - i] = WMMA_Load(TypeB, Inputs, 1024 + 32 - 32 * i, 64);
	}

	WMMA_Matrix c = WMMA_MatrixFill(TypeC, 0);

	[loop]
	for (int i = 0; i < 2; i++)
		c = WMMA_MatMulAcc(WMMA_F32_16X16X16_F16, as[i], bs[i], c);

	[loop]
	for (int i = 0; i < 2; i++)
		c = WMMA_MatMulAcc(WMMA_F32_16X16X16_F16, cs[i], ds[i], c);

	WMMA_Store(TypeC, Outputs, 0, 64, c);
}
