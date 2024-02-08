RWStructuredBuffer<float> Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    RWTexture2D<float> T = ResourceDescriptorHeap[0];
    Output[thr] = T.Load(int2(thr, 0));
}