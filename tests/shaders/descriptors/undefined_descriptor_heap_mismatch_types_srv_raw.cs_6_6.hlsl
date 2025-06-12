RWStructuredBuffer<float> Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    StructuredBuffer<float> T = ResourceDescriptorHeap[0];
    Output[thr] = T.Load(thr);
}