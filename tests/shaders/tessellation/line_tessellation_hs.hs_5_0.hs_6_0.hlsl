struct data
{
    float4 position : SV_Position;
    float3 color : COLOR;
    float line_density : LINE_DENSITY;
    float line_detail : LINE_DETAIL;
};

struct patch_constant_data
{
    float tess_factor[2] : SV_TessFactor;
    float3 color : COLOR;
    uint prim_id : PRIMITIVE_ID;
};

void patch_constant(OutputPatch<data, 1> control_points,
        uint prim_id : SV_PrimitiveID,
        out patch_constant_data output)
{
    output.tess_factor[0] = control_points[0].line_density;
    output.tess_factor[1] = control_points[0].line_detail;
    output.color = control_points[0].color;
    output.prim_id = prim_id;
}

[domain("isoline")]
[outputcontrolpoints(1)]
[partitioning("integer")]
[outputtopology("line")]
[patchconstantfunc("patch_constant")]
data main(InputPatch<data, 1> input)
{
    return input[0];
}