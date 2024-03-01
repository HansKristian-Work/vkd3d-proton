struct VsIn
{
    float4 position : POSITION;
};

struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
};

VsOut main(VsIn data)
{
    VsOut result;
    result.position = data.position;
    result.arg0 = float3(1.0f, 2.0f, 3.0f);
    result.arg1 = float2(4.0f, 5.0f);
    result.arg2 = uint4(6, 7, 8, 9);
    return result;
}
