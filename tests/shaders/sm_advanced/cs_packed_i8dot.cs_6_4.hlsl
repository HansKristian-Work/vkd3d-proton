RWStructuredBuffer<uint> RW : register(u0);

struct Inputs { uint a, b, acc; };
StructuredBuffer<Inputs> RO : register(t0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Inputs v = RO[id.x];
    uint acc = dot4add_i8packed(v.a, v.b, v.acc);
    RW[id.x] = acc;
}
