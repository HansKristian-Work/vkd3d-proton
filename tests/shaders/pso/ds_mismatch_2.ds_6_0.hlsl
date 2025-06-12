struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint2 arg2 : ARG2;
};

struct HsPatch
{
    float tf_outer[3] : SV_TESSFACTOR;
    float tf_inner : SV_INSIDETESSFACTOR;
};

[domain("tri")]
VsOut main(float3 coord : SV_DOMAINLOCATION,
        HsPatch patchconst, OutputPatch<VsOut, 3> patch)
{
    VsOut result;
    result.position = coord.x + patch[0].position +
            coord.y + patch[1].position +
            coord.z + patch[2].position;
    result.arg0 = patch[0].arg0;
    result.arg1 = patch[0].arg1;
    result.arg2 = patch[0].arg2.xyxy;
    return result;
}
