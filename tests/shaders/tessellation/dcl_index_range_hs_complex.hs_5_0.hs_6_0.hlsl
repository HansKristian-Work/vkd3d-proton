struct point_data_vs
{
    float4 positions[4] : FROG;
    uint p : PRIM;
};

struct point_data
{
    float4 position : SV_POSITION;
};

struct patch_constant_data
{
    float edges[4] : SV_TessFactor;
    float inside[2] : SV_InsideTessFactor;
};

patch_constant_data patch_constant(InputPatch<point_data_vs, 4> input)
{
    patch_constant_data output;

    output.edges[0] = 1.0f;
    output.edges[1] = 1.0f;
    output.edges[2] = 1.0f;
    output.edges[3] = 1.0f;
    output.inside[0] = 1.0f;
    output.inside[1] = 1.0f;

    return output;
}

[domain("quad")]
[outputcontrolpoints(4)]
[outputtopology("triangle_cw")]
[partitioning("integer")]
[patchconstantfunc("patch_constant")]
point_data main(InputPatch<point_data_vs, 4> input,
        uint i : SV_OutputControlPointID)
{
	point_data outp;
	outp.position = input[i].positions[input[0].p];
	return outp;
}

