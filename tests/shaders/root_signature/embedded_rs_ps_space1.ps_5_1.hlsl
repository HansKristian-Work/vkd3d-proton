RWStructuredBuffer<uint> U0 : register(u0, space1);
RWStructuredBuffer<uint> U1 : register(u1, space1);

[RootSignature("UAV(u0, space = 1), UAV(u1, space = 1)")]
void main()
{
	uint o;
	InterlockedAdd(U0[0], 1, o);
	InterlockedAdd(U1[0], 1, o);
}
