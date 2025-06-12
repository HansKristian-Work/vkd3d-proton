struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ArG0;
    float2 arg1 : arG1;
    uint4 arg2 : ARg2;
};

[maxvertexcount(3)]
void main(triangle VsOut vertices[3], inout PointStream<VsOut> out_stream)
{
    for (uint i = 0; i < 3; i++)
    {
        out_stream.Append(vertices[i]);
    }
}
