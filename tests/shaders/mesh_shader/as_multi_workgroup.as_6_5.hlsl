StructuredBuffer<uint> srv : register(t0);

struct Payload
{
    uint as_gid;
};

[numthreads(1,1,1)]
void main(in uint3 gid : SV_GroupID)
{
    Payload payload = { gid.x };
    DispatchMesh(srv[gid.x], 1, 1, payload);
}
