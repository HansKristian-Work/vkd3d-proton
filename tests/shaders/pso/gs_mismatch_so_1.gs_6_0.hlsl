struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
};

struct GsOut0
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    uint4 arg2 : ARG2;
};

struct GsOut1
{
    float4 position : SV_POSITION;
    float2 arg1 : ARG1;
};

[maxvertexcount(3)]
void main(triangle VsOut vertices[3],
    inout PointStream<GsOut0> out_stream0,
    inout PointStream<GsOut1> out_stream1)
{
    for (uint i = 0; i < 3; i++)
    {
        GsOut0 v0;
        v0.position = vertices[i].position;
        v0.arg0 = vertices[i].arg0;
        v0.arg2 = vertices[i].arg2;

        out_stream0.Append(v0);
    }

    GsOut1 v1;
    v1.position = vertices[0].position;
    v1.arg1 = vertices[0].arg1;

    out_stream1.Append(v1);
}
