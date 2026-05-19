//cbuffer Blah : register(b0) { uint4 a[2]; };
cbuffer Blah : register(b0) { uint4 a, b; };
RWStructuredBuffer<uint4> Output : register(u0);

[numthreads(32, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	uint4 values = uint4(0x123, 0x234, 0x345, 0x456);
	if (thr < 4)
	{
		values = b;
		//values = a[thr & 1];
	}
	Output[thr] = values;
}
