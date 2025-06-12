RWStructuredBuffer<uint> RWBuf : register(u0);
cbuffer C : register(b0) { uint4 v; };

[numthreads(1, 1, 1)]
void main()
{
        uint o;
        InterlockedAdd(RWBuf[0], v.x | v.y | v.z | v.w, o);
}