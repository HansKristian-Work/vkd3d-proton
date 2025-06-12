struct VertexData
{
    float4 pos : SV_POSITION;
    uint rt_data : UV_VIEWID;
};

[outputtopology("triangle")]
[numthreads(3, 1, 1)]
void main(in uint view_id : SV_VIEWID, in uint tid : SV_GROUPINDEX,
    out indices uint3 idx[3], out vertices VertexData vtx[1])
{
    SetMeshOutputCounts(3, 1);

    float2 coords = float2(tid & 2, (tid << 1) & 2);

    VertexData v;
    v.pos = float4(coords * float2(2, 2) + float2(-1, -1), 0, 1);
    v.rt_data = view_id;

    vtx[tid] = v;

    if (tid == 0)
        idx[tid] = uint3(0, 1, 2);
}
