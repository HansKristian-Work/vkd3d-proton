struct GsIo
{
    float4 pos : SV_POSITION;
};

struct HsPatch
{
    float tf_outer[2] : SV_TESSFACTOR;
};

[domain("isoline")]
GsIo main(in HsPatch patch_const, in float2 uv : SV_DomainLocation, const OutputPatch<GsIo, 4> in_patch)
{
    float4 a = lerp(in_patch[0].pos, in_patch[1].pos, uv.x);
    float4 b = lerp(in_patch[2].pos, in_patch[3].pos, uv.x);

    GsIo result;
    result.pos = lerp(a, b, uv.y);
    return result;
}
