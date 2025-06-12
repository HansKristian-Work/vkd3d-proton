struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
};

struct DsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
};

struct HsPatch
{
    float tf_outer[3] : SV_TESSFACTOR;
};

[domain("tri")]
DsOut main(float3 coord : SV_DOMAINLOCATION,
        HsPatch patchconst, OutputPatch<VsOut, 3> patch)
{
    DsOut result;
    result.position = coord.x + patch[0].position +
            coord.y + patch[1].position +
            coord.z + patch[2].position;
    result.arg0 = patch[0].arg0;
    result.arg1 = patch[0].arg1;
    result.arg2 = uint4(0, 0, 0, 0);
    return result;
}
