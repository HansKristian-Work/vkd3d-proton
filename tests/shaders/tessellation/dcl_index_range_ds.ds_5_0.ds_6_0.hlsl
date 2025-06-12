struct point_data
{
    float4 position : SV_POSITION;
};

struct patch_constant_data
{
    float edges[4] : SV_TessFactor;
    float inside[2] : SV_InsideTessFactor;
};

[domain("quad")]
point_data main(patch_constant_data input,
        float2 tess_coord : SV_DomainLocation,
        const OutputPatch<point_data, 4> patch)
{
    point_data output;

    float4 a = lerp(patch[0].position, patch[1].position, tess_coord.x);
    float4 b = lerp(patch[2].position, patch[3].position, tess_coord.x);
    output.position = lerp(a, b, tess_coord.y);

    return output;
}
