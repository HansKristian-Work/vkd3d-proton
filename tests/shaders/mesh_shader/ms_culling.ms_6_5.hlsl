StructuredBuffer<uint> buf : register(t0);

struct PrimData
{
    uint prim_id : UV_PRIMITIVE_ID;
};

groupshared uint primitive_count;

[numthreads(32,1,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, in uint3 gid : SV_GroupID, in uint3 did : SV_DispatchThreadID,
        out vertices float4 v[3] : SV_POSITION, out primitives PrimData p[32], out indices uint3 i[32])
{
    if (tid == 0)
        primitive_count = 0;

    GroupMemoryBarrierWithGroupSync();

    uint cull_mask = buf[gid.x];
    uint primitive_index = ~0u;

    if (cull_mask & (1u << tid))
        InterlockedAdd(primitive_count, 1u, primitive_index);

    GroupMemoryBarrierWithGroupSync();
    SetMeshOutputCounts(3, primitive_count);

    if (primitive_count == 0)
        return;

    if (tid < 3)
        v[tid] = float4(float(tid & 1) * 4.0f - 1.0f, float(tid & 2) * 2.0f - 1.0f, 0.0f, 1.0f);

    if (primitive_index != ~0u)
    {
        i[primitive_index] = uint3(0, 1, 2);
        p[primitive_index].prim_id = did.x;
    }
}
