struct GsIo
{
    float4 pos : SV_POSITION;
};

[maxvertexcount(3)]
void main(line GsIo gsIn[2], inout PointStream<GsIo> stream)
{
    for (uint i = 0; i < 2; i++)
        stream.Append(gsIn[i]);
}
