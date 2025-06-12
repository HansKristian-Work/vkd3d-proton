struct GsIo
{
    float4 pos : SV_POSITION;
};

GsIo main()
{
    GsIo result;
    result.pos = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return result;
}
