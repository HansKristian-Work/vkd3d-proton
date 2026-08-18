#define UVEC vector<uint, 8>

StructuredBuffer<UVEC> Inputs : register(t0);
RWStructuredBuffer<UVEC> Outputs : register(u0);

[numthreads(18, 1, 1)]
[WaveSize(32, 128, 32)]
void main(uint thr : SV_DispatchThreadID)
{
	UVEC inp = Inputs[3 * thr + 0];
	UVEC inp2 = Inputs[3 * thr + 1];
	UVEC inp3 = Inputs[3 * thr + 2];

	UVEC wave0 = WaveActiveBitAnd(inp);
	UVEC wave1 = WaveActiveBitOr(inp);
	UVEC wave2 = WaveActiveBitXor(inp);
	UVEC wave3 = WaveActiveProduct(inp);
	UVEC wave4 = WaveActiveSum(inp);
	UVEC wave5 = WaveActiveMin(inp);
	UVEC wave6 = WaveActiveMax(inp);
	UVEC wave7 = WavePrefixSum(inp);
	UVEC wave8 = WavePrefixProduct(inp);
	UVEC wave9 = WaveReadLaneAt(inp2, 9);
	UVEC wave10 = WaveReadLaneFirst(inp2);
	// Despite what you'd assume from docs, this is not a reduction.
	UVEC wave11 = WaveActiveAllEqual(inp) | (WaveActiveAllEqual(inp2) << 8) | (WaveActiveAllEqual(inp3) << 16);
	UVEC wave12 = (UVEC)0;
	wave12[0] = WaveMatch(inp).x;
	wave12[1] = WaveMatch(inp2).x;
	wave12[2] = WaveMatch(inp3).x;
	UVEC wave13 = WaveMultiPrefixBitAnd(inp, uint4(inp2[0], inp2[1], inp2[2], inp2[3]));
	UVEC wave14 = WaveMultiPrefixBitOr(inp, uint4(inp2[0], inp2[1], inp2[2], inp2[3]));
	UVEC wave15 = WaveMultiPrefixBitXor(inp, uint4(inp2[0], inp2[1], inp2[2], inp2[3]));
	UVEC wave16 = WaveMultiPrefixProduct(inp, uint4(inp2[0], inp2[1], inp2[2], inp2[3]));
	UVEC wave17 = WaveMultiPrefixSum(inp, uint4(inp2[0], inp2[1], inp2[2], inp2[3]));

	UVEC res = (UVEC)0;

	switch (thr)
	{
		case 0: res = wave0; break;
		case 1: res = wave1; break;
		case 2: res = wave2; break;
		case 3: res = wave3; break;
		case 4: res = wave4; break;
		case 5: res = wave5; break;
		case 6: res = wave6; break;
		case 7: res = wave7; break;
		case 8: res = wave8; break;
		case 9: res = wave9; break;
		case 10: res = wave10; break;
		case 11: res = wave11; break;
		case 12: res = wave12; break;
		case 13: res = wave13; break;
		case 14: res = wave14; break;
		case 15: res = wave15; break;
		case 16: res = wave16; break;
		case 17: res = wave17; break;
		default: break;
	}

	Outputs[thr] = res;
}
