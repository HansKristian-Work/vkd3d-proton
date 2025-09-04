RWStructuredBuffer<uint> U0 : register(u0, space0);
RWStructuredBuffer<uint> U1 : register(u1, space0);

struct Dummy {};

[maxvertexcount(1)]
void main(point Dummy dummy[1])
{
	uint o;
	InterlockedAdd(U0[0], 1, o);
	InterlockedAdd(U1[0], 1, o);
}
