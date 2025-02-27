struct GsIo
{
    float4 pos : SV_POSITION;
};

struct HsPatch
{
    float tf_outer[3] : SV_TESSFACTOR;
    float tf_inner : SV_INSIDETESSFACTOR;
};

[domain("tri")]
GsIo main(in HsPatch patch_const, in float3 bary : SV_DomainLocation, const OutputPatch<GsIo, 3> in_patch)
{
    GsIo result;
    result.pos = bary.x * in_patch[0].pos
               + bary.y * in_patch[1].pos
               + bary.z * in_patch[2].pos;
    return result;
}
