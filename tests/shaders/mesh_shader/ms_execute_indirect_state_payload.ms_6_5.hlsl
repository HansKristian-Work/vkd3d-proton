RWStructuredBuffer<uint> Buf : register(u0);

struct Payload
{
    uint count;
};

[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, in payload Payload p, out vertices float4 v[3] : SV_POSITION, out indices uint3 i[64])
{
    SetMeshOutputCounts(3, p.count);

    if (tid < 3)
        v[tid] = float4(float(tid & 1) * 4.0 - 1.0, 4.0 * float(tid & 2) - 1.0, 0.0, 1.0);

    if (tid < p.count)
        i[tid] = uint3(0, 1, 2);

    if (tid == 0)
    {
        uint o;
        InterlockedAdd(Buf[0], 1, o);
    }
}
