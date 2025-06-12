[numthreads(4,1,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, out vertices float4 v[4] : SV_POSITION, out indices uint3 i[2])
{
    SetMeshOutputCounts(4, 2);

    v[tid] = float4(float(tid & 1) * 2.0f - 1.0f, float(tid & 2) - 1.0f, 0.0f, 1.0f);

    if (tid < 2)
        i[tid] = uint3(tid, 1 + 2 * tid, 2);
}
