RWBuffer<uint> RWBuf : register(u0);

cbuffer Cbuf : register(b0)
{
	uint offset;
	uint count;
};

[numthreads(1, 1, 1)]
void main()
{
	uint o;
	InterlockedAdd(RWBuf[offset], count, o);
}
