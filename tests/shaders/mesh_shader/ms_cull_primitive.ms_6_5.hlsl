StructuredBuffer<uint> buf : register(t0);

struct PrimData
{
    uint prim_id : UV_PRIMITIVE_ID;
    bool do_cull : SV_CULLPRIMITIVE;
};

[numthreads(32,1,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, in uint3 gid : SV_GroupID, in uint3 did : SV_DispatchThreadID,
        out vertices float4 v[3] : SV_POSITION, out primitives PrimData p[32], out indices uint3 i[32])
{
    uint cull_mask = buf[gid.x];

    SetMeshOutputCounts(3, 32);

    if (tid < 3)
        v[tid] = float4(float(tid & 1) * 4.0f - 1.0f, float(tid & 2) * 2.0f - 1.0f, 0.0f, 1.0f);

    i[tid] = uint3(0, 1, 2);
    p[tid].prim_id = did.x;
    p[tid].do_cull = !(cull_mask & (1u << tid));
}
