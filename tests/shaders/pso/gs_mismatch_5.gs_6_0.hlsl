struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint arg2 : ARG2;
};

struct GsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
};

[maxvertexcount(3)]
void main(triangle VsOut vertices[3], inout PointStream<GsOut> out_stream)
{
    for (uint i = 0; i < 3; i++)
    {
        GsOut result;
        result.position = vertices[i].position;
        result.arg0 = vertices[i].arg0;
        result.arg1 = vertices[i].arg1;
        result.arg2 = vertices[i].arg2.xxxx;

        out_stream.Append(result);
    }
}
