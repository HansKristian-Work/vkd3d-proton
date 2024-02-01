struct OutputVertex
{
    float4 pos : SV_POSITION;
};

[numthreads(3,1,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, out vertices OutputVertex v[3])
{
    SetMeshOutputCounts(3, 1);
    v[tid].pos = float4(0.0f, 0.0f, 0.0f, 1.0f);
}
