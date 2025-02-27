struct GsIo
{
    float4 pos : SV_POSITION;
};

[maxvertexcount(3)]
void main(lineadj GsIo gsIn[4], inout PointStream<GsIo> stream)
{
    for (uint i = 0; i < 4; i++)
        stream.Append(gsIn[i]);
}
