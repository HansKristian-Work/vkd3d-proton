struct point_data
{
    float4 position : SV_POSITION;
};

struct patch_constant_data
{
    float edges[4] : SV_TessFactor;
    float inside[2] : SV_InsideTessFactor;
};

float4 tess_factors;
float2 inside_tess_factors;

patch_constant_data patch_constant(InputPatch<point_data, 3> input)
{
    patch_constant_data output;

    output.edges[0] = tess_factors.x;
    output.edges[1] = tess_factors.y;
    output.edges[2] = tess_factors.z;
    output.edges[3] = tess_factors.w;
    output.inside[0] = inside_tess_factors.x;
    output.inside[1] = inside_tess_factors.y;

    return output;
}

[domain("quad")]
[outputcontrolpoints(4)]
[outputtopology("triangle_cw")]
[partitioning("integer")]
[patchconstantfunc("patch_constant")]
point_data main(InputPatch<point_data, 3> input,
        uint i : SV_OutputControlPointID)
{
    return input[i];
}

