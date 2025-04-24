struct data
{
    float4 position : SV_Position;
};

struct patch_constant_data
{
    float edges[4] : SV_TessFactor;
    float inside[2] : SV_InsideTessFactor;
    float3 center : CENTER;
};

void patch_constant(OutputPatch<data, 4> control_points,
        out patch_constant_data output)
{
    uint i;

    output = (patch_constant_data)0;

    for (i = 0; i < 4; ++i)
    {
        output.edges[i] = 1.0;
    }

    for (i = 0; i < 2; ++i)
    {
        output.inside[i] = 1.0;
    }

    for (i = 0; i < 4; ++i)
    {
        output.center += control_points[i].position.xyz;
    }
    output.center /= 4;
}

[domain("quad")]
[outputcontrolpoints(4)]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant")]
data main(InputPatch<data, 1> input,
        uint i : SV_OutputControlPointID)
{
    data o = (data)0;
    const float4 vertices[] =
    {
        float4(0, 0, 0, 1),
        float4(0, 2, 0, 1),
        float4(2, 0, 0, 1),
        float4(2, 2, 0, 1),
    };

    o.position = input[0].position + vertices[i];

    return o;
}