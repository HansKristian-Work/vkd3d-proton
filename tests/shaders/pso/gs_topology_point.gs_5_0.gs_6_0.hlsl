struct GsIo
{
    float4 pos : SV_POSITION;
};

[maxvertexcount(3)]
void main(point GsIo gsIn[1], inout PointStream<GsIo> stream)
{
    stream.Append(gsIn[0]);
}
