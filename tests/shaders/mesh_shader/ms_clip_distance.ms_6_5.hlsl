struct OutputVertex
{
    float4 pos : SV_POSITION;
    float clip : SV_CLIPDISTANCE;
};

cbuffer input_data : register(b2)
{
    float clip;
};

[numthreads(4,1,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, out vertices OutputVertex v[4], out indices uint3 i[2])
{
    SetMeshOutputCounts(4, 2);

    v[tid].pos = float4(float(tid & 1) * 2.0f - 1.0f, float(tid & 2) - 1.0f, 0.0f, 1.0f);
    v[tid].clip = clip - float(tid);

    if (tid < 2)
        i[tid] = uint3(tid, 1 + 2 * tid, 2);
}
