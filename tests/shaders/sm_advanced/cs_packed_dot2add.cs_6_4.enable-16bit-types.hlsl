RWStructuredBuffer<uint> RW : register(u0);

struct Inputs { uint a, b, acc; };
StructuredBuffer<Inputs> RO : register(t0);

[numthreads(1, 1, 1)]
void main_u8dot(uint3 id : SV_DispatchThreadID)
{
    Inputs v = RO[id.x];
    uint acc = dot4add_u8packed(v.a, v.b, v.acc);
    RW[id.x] = acc;
}

[numthreads(1, 1, 1)]
void main_i8dot(uint3 id : SV_DispatchThreadID)
{
    Inputs v = RO[id.x];
    uint acc = dot4add_i8packed(v.a, v.b, v.acc);
    RW[id.x] = acc;
}

half2 ashalf2(uint v)
{
    // Apparently there is no ashalf(), sigh ...
    return half2(f16tof32(uint16_t(v)), f16tof32(uint16_t(v >> 16)));
}

uint asuint(half2 v)
{
    return uint(asuint16(v.x)) | (uint(asuint16(v.y)) << 16);
}

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Inputs v = RO[id.x];
    precise float result = dot2add(ashalf2(v.a), ashalf2(v.b), asfloat(v.acc));
    uint acc = asuint(result);
    RW[id.x] = acc;
}