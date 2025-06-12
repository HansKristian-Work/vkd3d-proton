struct VsOut
{
    float4 position : SV_POSITION;
    float3 arg0 : ARG0;
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
    result.color1 = float2(0.0f, 0.0f);
    result.color2 = uint4(data.primid, 0, 0, 0);
    return result;
}
