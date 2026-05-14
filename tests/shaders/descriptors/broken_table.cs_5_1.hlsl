Buffer<float> Buf : register(t10);
Buffer<float> Buf2 : register(t0, space1);
RWStructuredBuffer<float> RWStruct : register(u0);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	RWStruct[thr] = Buf.Load(0) + Buf2.Load(0) + 20;
}
