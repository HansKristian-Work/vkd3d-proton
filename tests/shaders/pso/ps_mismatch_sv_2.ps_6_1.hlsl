struct VsOut
{
    float3 bary_coord : SV_BARYCENTRICS;
    uint sample_index : SV_SAMPLEINDEX;
    bool front_face : SV_ISFRONTFACE;
    float4 position : SV_POSITION;
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
    result.color0 = float4(data.bary_coord, 0.0f);
    result.color1 = data.position.xy;
    result.color2 = uint4(data.sample_index, data.front_face, 0, 0);
    return result;
}
