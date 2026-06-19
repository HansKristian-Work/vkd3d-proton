struct Input
{
	float a;
	float b;
	float c;
	float d;
};

StructuredBuffer<Input> Inputs;

struct Output
{
	float fp32;
	float fp16_native;
	float fp32_legacy_fast;
	float fp32_legacy_precise;
};

RWStructuredBuffer<Output> Outputs;

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Input inp = Inputs[thr];

	// Make sure that impl gets its input directly from arithmetic operation.
	precise float ALU0 = inp.a + inp.b;
	precise float ALU1 = inp.c + inp.d;

	float16_t ALU016 = float16_t(ALU0);
	float16_t ALU116 = float16_t(ALU1);

	Output outp;
	outp.fp32 = ALU0 + ALU1;
	// We have to observe the truncate and expand. In native LLVM, these are never marked fast math.
	outp.fp16_native = float(ALU016) + float(ALU116);

	// NV optimizes away the roundtrip by default unless we do shenanigans.
	// The DXIL op won't be marked precise, so it's likely legal?
	outp.fp32_legacy_fast = f16tof32(f32tof16(ALU0)) + f16tof32(f32tof16(ALU1));

	// HZD case that is important to deal with. We have to enforce the quantization.
	uint ALU0_packed = f32tof16(ALU0);
	uint ALU1_packed = f32tof16(ALU1);
	precise float ALU0_unpacked = f16tof32(ALU0_packed);
	precise float ALU1_unpacked = f16tof32(ALU1_packed);
	outp.fp32_legacy_precise = ALU0_unpacked + ALU1_unpacked;

	Outputs[thr] = outp;
}
