struct DsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
};

struct HsOut
{
    float4 position : SV_POSITION;
};

struct HsPatch
{
    float tf_outer[3] : SV_TESSFACTOR;
    float tf_inner : SV_INSIDETESSFACTOR;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint2 arg2 : ARG2;
};

[domain("tri")]
DsOut main(float3 coord : SV_DOMAINLOCATION,
        HsPatch patchconst, OutputPatch<HsOut, 3> patch)
{
    DsOut result;
    result.position = coord.x + patch[0].position +
            coord.y + patch[1].position +
            coord.z + patch[2].position;
    result.arg0 = patchconst.arg0;
    result.arg1 = patchconst.arg1;
    result.arg2 = patchconst.arg2.xyxy;
    return result;
}
