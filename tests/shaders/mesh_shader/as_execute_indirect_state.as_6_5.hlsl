RWStructuredBuffer<uint> Buf : register(u0);

struct Payload
{
    uint count;
};

cbuffer Cbuf : register(b0) { uint3 groups; uint count; };

groupshared Payload p;

[numthreads(1, 1, 1)]
    void main()
{
    p.count = count;
    uint o;
    InterlockedAdd(Buf[0], 1, o);
    DispatchMesh(groups.x, groups.y, groups.z, p);
}
