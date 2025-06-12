RWStructuredBuffer<uint> RWBuf : register(u0);
cbuffer Cbuf : register(b0) { uint DrawID; };

void main()
{
	uint o;
	InterlockedAdd(RWBuf[DrawID], 1, o);
}
