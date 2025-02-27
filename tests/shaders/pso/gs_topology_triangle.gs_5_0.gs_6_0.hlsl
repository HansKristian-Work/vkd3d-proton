struct GsIo
{
    float4 pos : SV_POSITION;
};

[maxvertexcount(3)]
void main(triangle GsIo gsIn[3], inout PointStream<GsIo> stream)
{
    for (uint i = 0; i < 3; i++)
        stream.Append(gsIn[i]);
}
