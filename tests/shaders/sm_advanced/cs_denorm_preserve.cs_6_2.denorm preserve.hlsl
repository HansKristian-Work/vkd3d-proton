RWStructuredBuffer<float> RW : register(u0);
StructuredBuffer<float> RO : register(t0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float v = RO[id.x];
    v += v;
    RW[id.x] = v;
}
