struct GsIo
{
    float4 pos : SV_POSITION;
};

struct HsPatch
{
    float tf_outer[3] : SV_TESSFACTOR;
    float tf_inner : SV_INSIDETESSFACTOR;
};

HsPatch main_patch()
{
    HsPatch result;
    result.tf_outer[0] = 4.0f;
    result.tf_outer[1] = 4.0f;
    result.tf_outer[2] = 4.0f;
    result.tf_inner = 4.0f;
    return result;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("main_patch")]
GsIo main(uint cid : SV_OutputControlPointID, InputPatch<GsIo, 3> in_patch)
{
    return in_patch[cid];
}
