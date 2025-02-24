RWStructuredBuffer<float2> Buf : register(u0);

[numthreads(8, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
	Buf[id + 1] += float2(1.0, 2.0);
}
