#define FVEC vector<float, 8>
#define DVEC vector<double, 8>
#define IVEC vector<int, 8>
#define BVEC vector<bool, 8>

StructuredBuffer<FVEC> Inputs : register(t0);
RWStructuredBuffer<FVEC> Outputs : register(u0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	FVEC inp = Inputs[3 * thr + 0];
	FVEC inp2 = Inputs[3 * thr + 1];
	FVEC inp3 = Inputs[3 * thr + 2];
	FVEC res = (FVEC)0;

	FVEC deriv_coarse_x;
	FVEC deriv_coarse_y;
	FVEC deriv_fine_x;
	FVEC deriv_fine_y;

	// Workaround DXC being broken and sinks derivatives into the switch ...
	[branch]
	if (QuadAny(thr == 28))
	{
		deriv_coarse_x = ddx_coarse(inp);
		deriv_coarse_y = ddy_coarse(inp);
		deriv_fine_x = ddx_fine(inp);
		deriv_fine_y = ddy_fine(inp);
	}

	FVEC quad_read_lane_at;
	FVEC quad_read_across_x;
	FVEC quad_read_across_y;
	FVEC quad_read_across_diag;

	[branch]
	if (QuadAny(thr == 56))
	{
		quad_read_lane_at = QuadReadLaneAt(inp, 1);
		quad_read_across_x = QuadReadAcrossX(inp);
		quad_read_across_y = QuadReadAcrossY(inp);
		quad_read_across_diag = QuadReadAcrossDiagonal(inp);
	}

	switch (thr)
	{
		case 0: res = abs(inp); break;
		case 1: res = saturate(inp); break;
		case 2: res = FVEC(isnan(inp)); break;
		case 3: res = FVEC(isinf(inp)); break;
		case 4: res = FVEC(isfinite(inp)); break;
		case 5: res = FVEC(isnormal(inp)); break;
		case 6: res = cos(inp); break;
		case 7: res = sin(inp); break;
		case 8: res = tan(inp); break;
		case 9: res = acos(inp); break;
		case 10: res = asin(inp); break;
		case 11: res = atan(inp); break;
		case 12: res = cosh(inp); break;
		case 13: res = sinh(inp); break;
		case 14: res = tanh(inp); break;
		case 15: res = exp2(inp); break;
		case 16: res = frac(inp); break;
		case 17: res = log2(inp); break;
		case 18: res = sqrt(inp); break;
		case 19: res = rsqrt(inp); break;
		case 20: res = round(inp); break;
		case 21: res = trunc(inp); break;
		case 22: res = ceil(inp); break;
		case 23: res = floor(inp); break;
		case 24: res = max(inp, inp2); break;
		case 25: res = min(inp, inp2); break;
		case 26: res = mad(inp, inp2, inp3); break;
		case 27: res = FVEC(fma(DVEC(inp), DVEC(inp2), DVEC(inp3))); break;
		case 28: res = deriv_coarse_x; break;
		case 29: res = deriv_coarse_y; break;
		case 30: res = deriv_fine_x; break;
		case 31: res = deriv_fine_y; break;
		case 32: res = atan2(inp, inp2); break;
		case 33: res = degrees(inp); break;
		case 34: res = radians(inp); break;
		case 35: res = clamp(inp, inp2, inp3); break;
		case 36: res = fmod(inp, inp2); break;
		case 37: res = lerp(inp, inp2, inp3); break;
		case 38: res = pow(inp, inp2); break;
		case 39: res = rcp(inp); break;
		case 40: res = sign(inp); break;
		case 41: res = smoothstep(inp, inp2, inp3); break;
		case 42: res = step(inp, inp2); break;
		case 43: { IVEC iv; res = modf(inp, iv); res -= FVEC(iv); } break;
		case 44: res[0] = dot(inp, inp2); res[1] = dot(inp, inp3); res[2] = dot(inp2, inp3); break;
		case 45: res = asfloat(reversebits(IVEC(inp))); break;
		case 46: res = asfloat(countbits(IVEC(inp))); break;
		case 47: res = asfloat(firstbithigh(IVEC(inp))); break;
		case 48: res = asfloat(firstbitlow(IVEC(inp))); break;
		case 49: res = f16tof32(IVEC(inp)); break;
		case 50: res = asfloat(f32tof16(inp)); break;
		case 51: res = FVEC(and(BVEC(inp), BVEC(inp2))); break;
		case 52: res = FVEC(or(BVEC(inp), BVEC(inp2))); break;
		case 53: res = FVEC(select(BVEC(inp), inp2, inp3)); break;
		case 54: res[0] = float(all(BVEC(inp))); res[1] = float(all(BVEC(inp2))); res[2] = float(all(BVEC(inp3))); break;
		case 55: res[0] = float(any(BVEC(inp))); res[1] = float(any(BVEC(inp2))); res[2] = float(any(BVEC(inp3))); break;
		case 56: res = quad_read_lane_at; break;
		case 57: res = quad_read_across_x; break;
		case 58: res = quad_read_across_y; break;
		case 59: res = quad_read_across_diag; break;
		default: break;
	}

	Outputs[thr] = res;
}
