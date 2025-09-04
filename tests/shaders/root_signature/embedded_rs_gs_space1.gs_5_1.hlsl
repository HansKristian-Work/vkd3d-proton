RWStructuredBuffer<uint> U0 : register(u0, space1);
RWStructuredBuffer<uint> U1 : register(u1, space1);

struct Dummy {};

[RootSignature("UAV(u0, space = 1), UAV(u1, space = 1)")]
[maxvertexcount(1)]
void main(point Dummy dummy[1])
{
	uint o;
	InterlockedAdd(U0[0], 1, o);
	InterlockedAdd(U0[1], 1, o);
}
