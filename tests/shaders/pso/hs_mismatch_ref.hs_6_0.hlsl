struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
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
VsOut main(uint cid : SV_OutputControlPointID, InputPatch<VsOut, 3> in_patch)
{
    return in_patch[cid];
}
