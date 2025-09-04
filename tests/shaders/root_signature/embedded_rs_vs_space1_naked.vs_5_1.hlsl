RWStructuredBuffer<uint> U0 : register(u0, space1);
RWStructuredBuffer<uint> U1 : register(u1, space1);

float4 main() : SV_Position
{
	uint o;
	InterlockedAdd(U0[0], 1, o);
	InterlockedAdd(U1[0], 1, o);
	return 1.0.xxxx;
}
