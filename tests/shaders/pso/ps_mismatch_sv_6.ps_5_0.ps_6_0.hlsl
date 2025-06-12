struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    nointerpolation float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
    uint primid : SV_PRIMITIVEID;
};

struct PsOut
{
    float4 color0 : SV_TARGET0;
    float2 color1 : SV_TARGET1;
    uint4  color2 : SV_TARGET2;
};

PsOut main(VsOut data)
{
    PsOut result;
    result.color0 = float4(data.arg0, 0.0f);
    result.color1 = data.arg1;
    result.color2 = data.arg2 + data.primid;
    return result;
}
