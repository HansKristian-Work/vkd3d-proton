struct data
{
    float4 position : SV_Position;
};

struct patch_constant_data
{
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

void patch_constant(out patch_constant_data output)
{
    output.edges[0] = output.edges[1] = output.edges[2] = 1.0f;
    output.inside = 1.0f;
}

[domain("tri")]
[outputcontrolpoints(3)]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant")]
data main(uint i : SV_OutputControlPointID)
{
    data output;

    if (i == 0)
        output.position = float4(-1, 1, 0, 1);
    else if (i == 1)
        output.position = float4(3, 1, 0, 1);
    else
        output.position = float4(-1, -3, 0, 1);

    return output;
}

