RWStructuredBuffer<float> Output : register(u0);

struct Data { float v[1024]; };

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    ConstantBuffer<Data> T = ResourceDescriptorHeap[0];
    Output[thr] = T.v[4 + thr];
}