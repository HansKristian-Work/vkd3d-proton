struct data
{
    float4 position : SV_Position;
};

struct patch_constant_data
{
    float tess_factor[2] : SV_TessFactor;
    float3 a : A;
    float3 b : B;
};

void patch_constant(OutputPatch<data, 1> control_points,
        uint prim_id : SV_PrimitiveID,
        out patch_constant_data output)
{
    output.tess_factor[0] = 2.0;
    output.tess_factor[1] = 1.0;
    output.a = float3(2, 4, 10);
    output.b = float3(3, 3, 12);
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