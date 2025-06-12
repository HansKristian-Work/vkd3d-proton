RWStructuredBuffer<uint> uav : register(u1);

struct Payload
{
    uint as_gid;
};

[outputtopology("triangle")]
[numthreads(1,1,1)]
void main(in payload Payload p, in uint3 gid : SV_GroupID)
{
    InterlockedOr(uav[p.as_gid], 1u << gid.x);
    SetMeshOutputCounts(0, 0);
}
