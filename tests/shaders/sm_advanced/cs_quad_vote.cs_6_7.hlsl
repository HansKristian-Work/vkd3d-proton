StructuredBuffer<uint> A;
RWStructuredBuffer<uint4> B;

[numthreads(4, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    uint v = A[id];
    bool effect = bool(v & 2);
    uint4 R;
    R.x = QuadAny(effect);
    R.y = QuadAll(effect);

    [branch]
    if (v & 1)
    {
        R.z = QuadAny(effect);
        R.w = QuadAll(effect);
    }
    else
    {
        R.z = ~0u;
        R.w = ~0u;
    }

    B[id] = R;
}
