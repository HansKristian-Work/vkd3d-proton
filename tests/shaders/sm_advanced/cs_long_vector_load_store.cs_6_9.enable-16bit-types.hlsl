#define FVEC5 vector<float16_t, 5>
#define FVEC6 vector<float16_t, 6>
#define FVEC7 vector<float16_t, 7>
#define FVEC8 vector<float16_t, 8>
#define FVEC16 vector<float16_t, 16>

StructuredBuffer<FVEC5> Inputs5 : register(t0);
StructuredBuffer<FVEC6> Inputs6 : register(t1);
StructuredBuffer<FVEC7> Inputs7 : register(t2);
StructuredBuffer<FVEC8> Inputs8 : register(t3);
StructuredBuffer<FVEC16> Inputs16 : register(t4);
ByteAddressBuffer InputsBAB : register(t5);

RWStructuredBuffer<FVEC5> Outputs5 : register(u0);
RWStructuredBuffer<FVEC6> Outputs6 : register(u1);
RWStructuredBuffer<FVEC7> Outputs7 : register(u2);
RWStructuredBuffer<FVEC8> Outputs8 : register(u3);
RWStructuredBuffer<FVEC16> Outputs16 : register(u4);
RWByteAddressBuffer OutputsBAB : register(u5);

[numthreads(6, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Outputs5[thr] = Inputs5[thr] + FVEC5(1, 2, 3, 4, 5);
	Outputs6[thr] = Inputs6[thr] + FVEC6(1, 2, 3, 4, 5, 6);
	Outputs7[thr] = Inputs7[thr] + FVEC7(1, 2, 3, 4, 5, 6, 7);
	Outputs8[thr] = Inputs8[thr] + FVEC8(1, 2, 3, 4, 5, 6, 7, 8);
	Outputs16[thr] = Inputs16[thr] + FVEC16(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);

	OutputsBAB.Store<FVEC5>(10 * (thr + 0), InputsBAB.Load<FVEC5>(10 * thr) + FVEC5(1, 2, 3, 4, 5));
	OutputsBAB.Store<FVEC6>(12 * (thr + 8), InputsBAB.Load<FVEC6>(12 * thr) + FVEC6(1, 2, 3, 4, 5, 6));
	OutputsBAB.Store<FVEC7>(14 * (thr + 16), InputsBAB.Load<FVEC7>(14 * thr) + FVEC7(1, 2, 3, 4, 5, 6, 7));
	OutputsBAB.Store<FVEC8>(16 * (thr + 24), InputsBAB.Load<FVEC8>(16 * thr) + FVEC8(1, 2, 3, 4, 5, 6, 7, 8));
	OutputsBAB.Store<FVEC16>(32 * (thr + 32), InputsBAB.Load<FVEC16>(32 * thr) + FVEC16(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16));
}
