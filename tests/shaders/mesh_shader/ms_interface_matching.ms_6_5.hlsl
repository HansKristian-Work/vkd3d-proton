struct OutputVertex
{
    float4 pos : SV_POSITION;
    float2 coord : UV_COORD;
    nointerpolation uint id : UV_VERTEX_ID;
};

struct OutputPrimitive
{
    float4 color : UV_COLOR;
    uint prim_data : UV_PRIMITIVE_DATA;
};

[numthreads(3,1,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, out vertices OutputVertex v[16], out indices uint3 i[16], out primitives OutputPrimitive p[16])
{
    SetMeshOutputCounts(3, 1);

    float2 coord = float2(float(tid & 1) * 2.0f, float(tid & 2));

    v[tid].pos = float4(coord * 2.0f - 1.0f, 0.0f, 1.0f);
    v[tid].coord = coord;
    v[tid].id = tid + 1;

    if (tid == 0)
    {
        i[0] = uint3(0, 1, 2);
        p[0].color = float4(0.0f, 1.0f, 0.0f, 1.0f);
        p[0].prim_data = 0xdeadbeef;
    }
}
