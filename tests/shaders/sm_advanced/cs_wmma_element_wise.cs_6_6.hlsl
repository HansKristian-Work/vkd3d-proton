RWByteAddressBuffer Outputs : register(u0);
ByteAddressBuffer Inputs : register(t0);
RWByteAddressBuffer MAGIC : register(u0, space2147420894);

#include "wmma_ags.h"

[numthreads(32, 1, 1)]
[WaveSize(32)]
void main(uint thr : SV_GroupIndex)
{
	// All FP16 variants work on native. No need to exhaustively test that on our end.
	// FP8 seems to segfault driver.
	WMMA_Type Type32 = WMMA_MakeType(WaveMatrixDataFormat_F32, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);
	WMMA_Type Type16 = WMMA_MakeType(WaveMatrixDataFormat_F16, WaveMatrixType_Accumulator, WaveMatrixShape_16X16, true);
	WMMA_Matrix a = WMMA_Load(Type32, Inputs, 0, 64);
	WMMA_Matrix b = WMMA_Load(Type32, Inputs, 1024, 64);

	WMMA_Matrix a16 = WMMA_Convert(Type32, Type16, a);
	WMMA_Matrix b16 = WMMA_Convert(Type32, Type16, b);

	WMMA_Matrix f32_0 = WMMA_ElementWiseOp(Type32, a, b, MatrixElementWiseOp_Add);
	WMMA_Matrix f32_1 = WMMA_ElementWiseOp(Type32, a, b, MatrixElementWiseOp_Sub);
	WMMA_Matrix f32_2 = WMMA_ElementWiseOp(Type32, a, b, MatrixElementWiseOp_Mul);
	WMMA_Matrix f32_3 = WMMA_ElementWiseOp(Type32, a, b, MatrixElementWiseOp_Div);
	WMMA_Matrix f32_4 = WMMA_ElementWiseOp(Type32, a, b, MatrixElementWiseOp_Times);

	WMMA_Matrix f16_0 = WMMA_ElementWiseOp(Type16, a16, b16, MatrixElementWiseOp_Add);
	WMMA_Matrix f16_1 = WMMA_ElementWiseOp(Type16, a16, b16, MatrixElementWiseOp_Sub);
	WMMA_Matrix f16_2 = WMMA_ElementWiseOp(Type16, a16, b16, MatrixElementWiseOp_Mul);
	WMMA_Matrix f16_3 = WMMA_ElementWiseOp(Type16, a16, b16, MatrixElementWiseOp_Div);
	WMMA_Matrix f16_4 = WMMA_ElementWiseOp(Type16, a16, b16, MatrixElementWiseOp_Times);

	WMMA_Store(Type32, Outputs, 1024 * 0, 64, f32_0);
	WMMA_Store(Type32, Outputs, 1024 * 1, 64, f32_1);
	WMMA_Store(Type32, Outputs, 1024 * 2, 64, f32_2);
	WMMA_Store(Type32, Outputs, 1024 * 3, 64, f32_3);
	WMMA_Store(Type32, Outputs, 1024 * 4, 64, f32_4);

	f16_0 = WMMA_Convert(Type16, Type32, f16_0);
	f16_1 = WMMA_Convert(Type16, Type32, f16_1);
	f16_2 = WMMA_Convert(Type16, Type32, f16_2);
	f16_3 = WMMA_Convert(Type16, Type32, f16_3);
	f16_4 = WMMA_Convert(Type16, Type32, f16_4);

	WMMA_Store(Type32, Outputs, 1024 * 5, 64, f16_0);
	WMMA_Store(Type32, Outputs, 1024 * 6, 64, f16_1);
	WMMA_Store(Type32, Outputs, 1024 * 7, 64, f16_2);
	WMMA_Store(Type32, Outputs, 1024 * 8, 64, f16_3);
	WMMA_Store(Type32, Outputs, 1024 * 9, 64, f16_4);
}
