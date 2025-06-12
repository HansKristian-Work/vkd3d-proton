struct VertOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    uint4 arg2 : ARG2;
};

struct PrimOut
{
    float2 arg1 : ARG1;
};

[outputtopology("triangle")]
[numthreads(3,1,1)]
void main(in uint tid : SV_GROUPINDEX,
        out vertices VertOut v[3], out indices uint3 i[1],
        out primitives PrimOut p[1])
{
    SetMeshOutputCounts(3, 1);

    VertOut vertex;
    /* Not drawing anything, just use some random non-constant positions */
    vertex.position = float4(tid & 1, tid & 2, 0.0f, 1.0f);
    vertex.arg0 = float3(1.0f, 2.0f, 3.0f);
    vertex.arg2 = uint4(6, 7, 8, 9);

    v[tid] = vertex;

    PrimOut prim;
    prim.arg1 = float2(4.0f, 5.0f);

    if (tid == 0)
    {
        i[0] = uint3(0, 1, 2);
        p[0] = prim;
    }
}
