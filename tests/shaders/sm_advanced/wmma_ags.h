#pragma once

static const uint MagicCodeShift   = 28;
static const uint MagicCodeMask    = 0xf;
static const uint OpcodePhaseShift = 24;
static const uint OpcodePhaseMask  = 0x3;
static const uint DataShift        = 8;
static const uint DataMask         = 0xffff;
static const uint OpcodeShift      = 0;
static const uint OpcodeMask       = 0xff;

static const uint MagicCode = 0x5;

static const uint WaveMatrixMulAcc            = 0x28;
static const uint WaveMatrixUavLoad           = 0x29;
static const uint WaveMatrixUavStore          = 0x2a;
static const uint WaveMatrixGlobalLoad        = 0x2b;
static const uint WaveMatrixGlobalStore       = 0x2c;
static const uint WaveMatrixLdsLoad           = 0x2d;
static const uint WaveMatrixLdsStore          = 0x2e;
static const uint WaveMatrixElementFill       = 0x2f;
static const uint WaveMatrixElementExtract    = 0x30;
static const uint WaveMatrixLength            = 0x31;
static const uint WaveMatrixCopy              = 0x32;
static const uint WaveMatrixFill              = 0x33;
static const uint Float8Conversion            = 0x36;

enum WaveMatrixOpDataFormat
{
    WaveMatrixDataFormat_I4   = 0x0,
    WaveMatrixDataFormat_U4   = 0x1,
    WaveMatrixDataFormat_I8   = 0x2,
    WaveMatrixDataFormat_U8   = 0x3,
    WaveMatrixDataFormat_F16  = 0x4,
    WaveMatrixDataFormat_BF16 = 0x5,
    WaveMatrixDataFormat_F32  = 0x6,
    WaveMatrixDataFormat_I32  = 0x7,
    WaveMatrixDataFormat_U32  = 0x8,
    WaveMatrixDataFormat_BF8  = 0x9,
    WaveMatrixDataFormat_FP8  = 0xa,
};

enum WaveMatrixOpMatrixType
{
    WaveMatrixType_A            = 0x0,
    WaveMatrixType_B            = 0x1,
    WaveMatrixType_Accumulator  = 0x2,
};

enum WaveMatrixOpMatrixShape
{
    WaveMatrixShape_16X16 = 0x0,
    WaveMatrixShape_32X16 = 0x1,
    WaveMatrixShape_16X32 = 0x2,
    WaveMatrixShape_64X16 = 0x3,
};

enum WaveMatrixOpcode
{
    WMMA_BF16_16X16X16_BF16     = 0x0,
    WMMA_F16_16X16X16_F16       = 0x1,
    WMMA_F32_16X16X16_BF16      = 0x2,
    WMMA_F32_16X16X16_BF8_BF8   = 0x3,
    WMMA_F32_16X16X16_BF8_FP8   = 0x4,
    WMMA_F32_16X16X16_F16       = 0x5,
    WMMA_F32_16X16X16_FP8_BF8   = 0x6,
    WMMA_F32_16X16X16_FP8_FP8   = 0x7,
    WMMA_I32_16X16X16_I4        = 0x8,
    WMMA_I32_16X16X16_U4        = 0x9,
    WMMA_I32_16X16X16_IU4       = 0xa,
    WMMA_I32_16X16X16_UI4       = 0xb,
    WMMA_I32_16X16X16_I8        = 0xc,
    WMMA_I32_16X16X16_U8        = 0xd,
    WMMA_I32_16X16X16_IU8       = 0xe,
    WMMA_I32_16X16X16_UI8       = 0xf,
    WMMA_I32_16X16X32_I4        = 0x10,
    WMMA_I32_16X16X32_U4        = 0x11,
    WMMA_I32_16X16X32_IU4       = 0x12,
    WMMA_I32_16X16X32_UI4       = 0x13,
    SWMMA_BF16_16X16X32_BF16    = 0x14,
    SWMMA_F16_16X16X32_F16      = 0x15,
    SWMMA_F32_16X16X32_BF16     = 0x16,
    SWMMA_F32_16X16X32_BF8_BF8  = 0x17,
    SWMMA_F32_16X16X32_BF8_FP8  = 0x18,
    SWMMA_F32_16X16X32_F16      = 0x19,
    SWMMA_F32_16X16X32_FP8_BF8  = 0x1a,
    SWMMA_F32_16X16X32_FP8_FP8  = 0x1b,
    SWMMA_I32_16X16X32_I4       = 0x1c,
    SWMMA_I32_16X16X32_U4       = 0x1d,
    SWMMA_I32_16X16X32_IU4      = 0x1e,
    SWMMA_I32_16X16X32_UI4      = 0x1f,
    SWMMA_I32_16X16X32_I8       = 0x20,
    SWMMA_I32_16X16X32_U8       = 0x21,
    SWMMA_I32_16X16X32_IU8      = 0x22,
    SWMMA_I32_16X16X32_UI8      = 0x23,
    SWMMA_I32_16X16X64_I4       = 0x24,
    SWMMA_I32_16X16X64_U4       = 0x25,
    SWMMA_I32_16X16X64_IU4      = 0x26,
    SWMMA_I32_16X16X64_UI4      = 0x27,
};

enum WaveMatrixRegType
{
    WaveMatrixRegType_RetVal_Reg          = 0x0,
    WaveMatrixRegType_A_TempReg           = 0x1,
    WaveMatrixRegType_B_TempReg           = 0x2,
    WaveMatrixRegType_Accumulator_TempReg = 0x3,
};

enum MatrixElementWiseOp
{
    MatrixElementWiseOp_Add   = 0x1,
    MatrixElementWiseOp_Sub   = 0x2,
    MatrixElementWiseOp_Mul   = 0x3,
    MatrixElementWiseOp_Div   = 0x4,
    MatrixElementWiseOp_Times = 0x5,
};

enum SparsityIndexMem
{
    SparsityIndexMem_UavBuffer    = 0x0,
    SparsityIndexMem_GroupShared  = 0x1,
    SparsityIndexMem_GlobalBuffer = 0x2,
};

static const uint WaveMatrixOpcode_OpsShift  = 0;
static const uint WaveMatrixOpcode_OpsMask   = 0x7f;
static const uint WaveMatrixOpcode_FlagShift = 15;
static const uint WaveMatrixOpcode_FlagMask  = 0x1;

static const uint WaveMatrixInOut_ChannelShift        = 0;
static const uint WaveMatrixInOut_ChannelMask         = 0xf;
static const uint WaveMatrixInOut_SecondRegFlagShift  = 4;
static const uint WaveMatrixInOut_SecondRegFlagMask   = 0xf;
static const uint WaveMatrixInOut_MatRegTypeFlagShift = 8;
static const uint WaveMatrixInOut_MatRegTypeFlagMask  = 0xff;

static const uint WaveMatrixModifier_DataFormatFlagShift = 0;
static const uint WaveMatrixModifier_DataFormatFlagMask  = 0xf;
static const uint WaveMatrixModifier_MatrixTypeFlagShift = 4;
static const uint WaveMatrixModifier_MatrixTypeFlagMask  = 0x7;
static const uint WaveMatrixModifier_LayoutFlagShift     = 7;
static const uint WaveMatrixModifier_LayoutFlagMask      = 0x1;
static const uint WaveMatrixModifier_ShapeShift          = 8;
static const uint WaveMatrixModifier_ShapeMask           = 0x7;
static const uint WaveMatrixModifier_MatrixTileShift     = 11;
static const uint WaveMatrixModifier_MatrixTileMask      = 0x1;
static const uint WaveMatrixModifier_IndexMemTypeShift   = 14;
static const uint WaveMatrixModifier_IndexMemTypeMask    = 0x3;

// For wave32 and FP32, we need up to 8 values to store a 16x16 matrix.
struct WMMA_Matrix
{
	uint v[8];
};

uint Code(uint opcode, uint opcodePhase, uint immediateData)
{
    return (MagicCode << MagicCodeShift) |
           ((immediateData & DataMask) << DataShift) |
           ((opcodePhase & OpcodePhaseMask) << OpcodePhaseShift) |
           ((opcode & OpcodeMask) << OpcodeShift);
}

uint AGSMagic(uint code, uint arg0, uint arg1)
{
	uint ret;
	MAGIC.InterlockedCompareExchange(code, arg0, arg1, ret);
	return ret;
}

uint AGSMagic(uint opcode, uint phase, uint imm, uint arg0, uint arg1)
{
	return AGSMagic(Code(opcode, phase, imm), arg0, arg1);
}

uint MatrixIO(uint channel, uint reg, uint type)
{
	return (channel << WaveMatrixInOut_ChannelShift) |
		(reg << WaveMatrixInOut_SecondRegFlagShift) |
		(type << WaveMatrixInOut_MatRegTypeFlagShift);
}

WMMA_Matrix WMMA_MatMulAcc(WaveMatrixOpcode op, WMMA_Matrix A, WMMA_Matrix B, WMMA_Matrix C)
{
	// A matrix
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(0, 0, WaveMatrixRegType_A_TempReg), A.v[0], A.v[1]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(1, 0, WaveMatrixRegType_A_TempReg), A.v[2], A.v[3]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(0, 1, WaveMatrixRegType_A_TempReg), A.v[4], A.v[5]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(1, 1, WaveMatrixRegType_A_TempReg), A.v[6], A.v[7]);

	// B matrix
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(0, 0, WaveMatrixRegType_B_TempReg), B.v[0], B.v[1]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(1, 0, WaveMatrixRegType_B_TempReg), B.v[2], B.v[3]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(0, 1, WaveMatrixRegType_B_TempReg), B.v[4], B.v[5]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(1, 1, WaveMatrixRegType_B_TempReg), B.v[6], B.v[7]);

	// C matrix
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(0, 0, WaveMatrixRegType_Accumulator_TempReg), C.v[0], C.v[1]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(1, 0, WaveMatrixRegType_Accumulator_TempReg), C.v[2], C.v[3]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(0, 1, WaveMatrixRegType_Accumulator_TempReg), C.v[4], C.v[5]);
	AGSMagic(WaveMatrixMulAcc, 0, MatrixIO(1, 1, WaveMatrixRegType_Accumulator_TempReg), C.v[6], C.v[7]);

	// Configure type
	AGSMagic(WaveMatrixMulAcc, 1, int(op) << int(WaveMatrixOpcode_OpsShift), 0, 0);

	// Read output
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixMulAcc, 2, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

struct WMMA_Type
{
	uint code;
};

WMMA_Type WMMA_MakeType(WaveMatrixOpDataFormat fmt, WaveMatrixOpMatrixType mtype, WaveMatrixOpMatrixShape shape, bool transposed)
{
	WMMA_Type type;
	type.code = (int(fmt) << WaveMatrixModifier_DataFormatFlagShift) |
		(int(mtype) << WaveMatrixModifier_MatrixTypeFlagShift) |
		(int(shape) << WaveMatrixModifier_ShapeShift) |
		(transposed << WaveMatrixModifier_LayoutFlagShift);
	return type;
}

WMMA_Matrix WMMA_Load(WMMA_Type type, ByteAddressBuffer BAB, uint offset, uint stride)
{
	uint doorbell = AGSMagic(WaveMatrixUavLoad, 0, 0, offset, stride);
	uint hook = BAB.Load(doorbell);
	AGSMagic(WaveMatrixUavLoad, 1, type.code, hook, 0);
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

WMMA_Matrix WMMA_Load(WMMA_Type type, RWByteAddressBuffer BAB, uint offset, uint stride)
{
	uint doorbell = AGSMagic(WaveMatrixUavLoad, 0, 0, offset, stride);
	uint hook = BAB.Load(doorbell);
	AGSMagic(WaveMatrixUavLoad, 1, type.code, hook, 0);
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixUavLoad, 2, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

void WMMA_Store(WMMA_Type type, RWByteAddressBuffer BAB, uint offset, uint stride, WMMA_Matrix m)
{
	uint doorbell = AGSMagic(WaveMatrixUavStore, 0, 0, offset, stride);
	uint hook = BAB.Load(doorbell);
	AGSMagic(WaveMatrixUavStore, 1, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), m.v[0], m.v[1]);
	AGSMagic(WaveMatrixUavStore, 1, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), m.v[2], m.v[3]);
	AGSMagic(WaveMatrixUavStore, 1, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), m.v[4], m.v[5]);
	AGSMagic(WaveMatrixUavStore, 1, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), m.v[6], m.v[7]);
	AGSMagic(WaveMatrixUavStore, 2, type.code, hook, 0);
}

groupshared uint LDS[512];

WMMA_Matrix WMMA_LoadLDS(WMMA_Type type, uint offset, uint stride)
{
	uint hook;
	uint doorbell = AGSMagic(WaveMatrixLdsLoad, 0, 0, offset, stride);
	InterlockedAdd(LDS[doorbell], 0, hook);
	AGSMagic(WaveMatrixLdsLoad, 1, type.code, hook, 0);
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixLdsLoad, 2, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

void WMMA_StoreLDS(WMMA_Type type, uint offset, uint stride, WMMA_Matrix m)
{
	uint doorbell = AGSMagic(WaveMatrixLdsStore, 0, 0, offset, stride);
	uint hook;
	InterlockedAdd(LDS[doorbell], 0, hook);
	AGSMagic(WaveMatrixLdsStore, 1, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), m.v[0], m.v[1]);
	AGSMagic(WaveMatrixLdsStore, 1, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), m.v[2], m.v[3]);
	AGSMagic(WaveMatrixLdsStore, 1, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), m.v[4], m.v[5]);
	AGSMagic(WaveMatrixLdsStore, 1, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), m.v[6], m.v[7]);
	AGSMagic(WaveMatrixLdsStore, 2, type.code, hook, 0);
}

WMMA_Matrix WMMA_Convert(WMMA_Type intype, WMMA_Type outtype, WMMA_Matrix m)
{
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(0, 0, 0), m.v[0], m.v[1]);
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(1, 0, 0), m.v[2], m.v[3]);
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(0, 1, 0), m.v[4], m.v[5]);
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(1, 1, 0), m.v[6], m.v[7]);
	AGSMagic(WaveMatrixCopy, 1, intype.code, outtype.code, 0);
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

WMMA_Matrix WMMA_ConvertSaturate(WMMA_Type intype, WMMA_Type outtype, WMMA_Matrix m)
{
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(0, 0, 0), m.v[0], m.v[1]);
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(1, 0, 0), m.v[2], m.v[3]);
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(0, 1, 0), m.v[4], m.v[5]);
	AGSMagic(WaveMatrixCopy, 0, MatrixIO(1, 1, 0), m.v[6], m.v[7]);
	AGSMagic(WaveMatrixCopy, 1, intype.code, outtype.code, 1);
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixCopy, 2, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

uint WMMA_MatrixLength(WMMA_Type type)
{
	return AGSMagic(WaveMatrixLength, 0, type.code, 0, 0);
}

uint WMMA_MatrixElementExtract(WMMA_Type type, WMMA_Matrix m, uint elem)
{
	AGSMagic(WaveMatrixElementExtract, 0, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), m.v[0], m.v[1]);
	AGSMagic(WaveMatrixElementExtract, 0, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), m.v[2], m.v[3]);
	AGSMagic(WaveMatrixElementExtract, 0, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), m.v[4], m.v[5]);
	AGSMagic(WaveMatrixElementExtract, 0, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), m.v[6], m.v[7]);
	return AGSMagic(WaveMatrixElementExtract, 1, type.code, elem, 0); 
}

WMMA_Matrix WMMA_MatrixElementFill(WMMA_Type type, WMMA_Matrix m, uint index, uint data)
{
	AGSMagic(WaveMatrixElementFill, 0, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), m.v[0], m.v[1]);
	AGSMagic(WaveMatrixElementFill, 0, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), m.v[2], m.v[3]);
	AGSMagic(WaveMatrixElementFill, 0, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), m.v[4], m.v[5]);
	AGSMagic(WaveMatrixElementFill, 0, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), m.v[6], m.v[7]);
	AGSMagic(WaveMatrixElementFill, 1, type.code, index, data);
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixElementFill, 2, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

WMMA_Matrix WMMA_MatrixFill(WMMA_Type type, uint value)
{
	AGSMagic(WaveMatrixFill, 0, type.code, value, 0);
	WMMA_Matrix ret;
	ret.v[0] = AGSMagic(WaveMatrixFill, 1, MatrixIO(0, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[1] = AGSMagic(WaveMatrixFill, 1, MatrixIO(1, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[2] = AGSMagic(WaveMatrixFill, 1, MatrixIO(2, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[3] = AGSMagic(WaveMatrixFill, 1, MatrixIO(3, 0, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[4] = AGSMagic(WaveMatrixFill, 1, MatrixIO(0, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[5] = AGSMagic(WaveMatrixFill, 1, MatrixIO(1, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[6] = AGSMagic(WaveMatrixFill, 1, MatrixIO(2, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	ret.v[7] = AGSMagic(WaveMatrixFill, 1, MatrixIO(3, 1, WaveMatrixRegType_RetVal_Reg), 0, 0);
	return ret;
}

