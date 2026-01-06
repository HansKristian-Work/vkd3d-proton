RWStructuredBuffer<uint> R[2] : register(u1, space0);
RWStructuredBuffer<uint> UnsizedR[] : register(u1, space1);

[numthreads(1, 1, 1)]
void main(uint gid : SV_GroupID)
{
	uint o;
	InterlockedAdd(R[gid][0], 1, o);
	InterlockedAdd(UnsizedR[gid][0], 1, o);
}
