float4 tess_factor;

struct hs_input
{
    float4 position : SV_Position;
};

struct hs_output
{
    float4 position : SV_Position;
    float  clip     : SV_ClipDistance;
};

struct patch_constant_data
{
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

void patch_constant(InputPatch<hs_input, 3> input, out patch_constant_data output)
{
    output.edges[0] = tess_factor.x;
    output.edges[1] = 1.0f;
    output.edges[2] = 1.0f;
    output.inside = tess_factor.y;
}

[domain("tri")]
[outputcontrolpoints(3)]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant")]
hs_output main(InputPatch<hs_input, 3> input, uint i : SV_OutputControlPointID)
{
    hs_output output;
    output.position = input[i].position;
    output.clip     = 1024.0;
    return output;
}

