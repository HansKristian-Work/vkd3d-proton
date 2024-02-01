struct PrimData
{
    float4 color : UV_COLOR;
    uint prim_id : SV_PRIMITIVEID;
    uint rt_layer : SV_RENDERTARGETARRAYINDEX;
};

[outputtopology("triangle")]
[numthreads(8,1,1)]
void main(in uint tid : SV_GROUPINDEX,
        out vertices float4 v[3] : SV_POSITION, out indices uint3 i[8], out primitives PrimData p[8])
{
    SetMeshOutputCounts(3, 8);

    if (tid == 0)
    {
        v[0] = float4(-1.0f, -1.0f, 0.0f, 1.0f);
        v[1] = float4( 3.0f, -1.0f, 0.0f, 1.0f);
        v[2] = float4(-1.0f,  3.0f, 0.0f, 1.0f);
    }

    i[tid] = uint3(0, 1, 2);

    p[tid].color = float4(float(tid & 1), float(tid & 2), float(tid & 4), 1.0f);
    p[tid].prim_id = (tid + 1) * (tid + 1);
    p[tid].rt_layer = tid;
}
