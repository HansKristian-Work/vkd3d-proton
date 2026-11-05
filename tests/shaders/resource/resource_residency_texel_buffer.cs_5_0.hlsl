RWBuffer<uint> R : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	R[thr] = 100;
}
