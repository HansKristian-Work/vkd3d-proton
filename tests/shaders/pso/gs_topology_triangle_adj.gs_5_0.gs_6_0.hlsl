struct GsIo
{
    float4 pos : SV_POSITION;
};

[maxvertexcount(3)]
void main(triangleadj GsIo gsIn[6], inout PointStream<GsIo> stream)
{
    for (uint i = 0; i < 6; i++)
        stream.Append(gsIn[i]);
}
