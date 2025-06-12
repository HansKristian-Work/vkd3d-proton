struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
    float arg1 : ARG1;
    uint2 arg2 : ARG2;
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
    result.color1 = data.arg1.xx;
    result.color2 = data.arg2.xyxy;
    return result;
}
