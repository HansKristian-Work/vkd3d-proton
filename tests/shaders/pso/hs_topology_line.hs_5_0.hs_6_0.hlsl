struct GsIo
{
    float4 pos : SV_POSITION;
};

struct HsPatch
{
    float tf_outer[2] : SV_TESSFACTOR;
};

HsPatch main_patch()
{
    HsPatch result;
    result.tf_outer[0] = 4.0f;
    result.tf_outer[1] = 4.0f;
    return result;
}

[domain("isoline")]
[partitioning("integer")]
[outputtopology("line")]
[outputcontrolpoints(4)]
[patchconstantfunc("main_patch")]
GsIo main(uint cid : SV_OutputControlPointID, InputPatch<GsIo, 4> in_patch)
{
    return in_patch[cid];
}
