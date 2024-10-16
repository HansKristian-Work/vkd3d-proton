RWStructuredBuffer<uint> RWBuf : register(u0);
cbuffer Cbuf : register(b0) { uint DrawID; };

[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void main()
{
	uint o;
	InterlockedAdd(RWBuf[DrawID], 1, o);
}
