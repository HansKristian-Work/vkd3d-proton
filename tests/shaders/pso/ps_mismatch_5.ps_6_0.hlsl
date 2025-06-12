struct VsOut
{
    float4 position : SV_POSITION;
    float2 arg0 : ARG0;
    float arg3 : ARG3;
    float2 arg1 : ARG1;
    uint4 arg2 : ARG2;
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
    result.color0 = float4(data.arg0, data.arg3, 0.0f);
    result.color1 = data.arg1;
    result.color2 = data.arg2;
    return result;
}
