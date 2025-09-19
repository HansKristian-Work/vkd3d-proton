struct C { uint v; };
ConstantBuffer<C> CBuf[]: register(b0, space0);

Texture2D<uint> ROTex2D[] : register(t0, space0);
Buffer<uint> ROTyped[] : register(t0, space1);
StructuredBuffer<uint> RORaw[] : register(t0, space2);

RWTexture2D<uint> RWTex2D[] : register(u0, space0);
RWBuffer<uint> RWTyped[] : register(u0, space1);
RWStructuredBuffer<uint> RWRaw[] : register(u0, space2);

RWStructuredBuffer<uint> RWOut : register(u0, space3);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	uint res = thr;
	res += RORaw[thr][0] != 0 ? 1000 : 0;
	res += RWRaw[thr][0] != 0 ? 10000 : 0;
	res += ROTyped[thr][0] != 0 ? 100000 : 0;
	res += RWTyped[thr][0] != 0 ? 1000000 : 0;
	res += ROTex2D[thr].Load(int3(thr, 0, 0)) != 0 ? 10000000 : 0;
	res += RWTex2D[thr].Load(int2(thr, 0)) != 0 ? 100000000 : 0;
	res += CBuf[thr].v != 0 ? 1000000000 : 0;
	RWOut[thr] = res;
}

