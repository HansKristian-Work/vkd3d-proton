struct data
{
    float4 position : SV_Position;
    float4 a : TEXCOORD0;
    float4 b : TEXCOORD1;
    float4 c : TEXCOORD2;
};

struct patch_constant_data
{
    float edges[3] : SV_TessFactor;
    float inside[1] : SV_InsideTessFactor;
};

void patch_constant(OutputPatch<data, 3> control_points,
        InputPatch<data, 3> vertices,
        out patch_constant_data output)
{
    float4 should_be_one = vertices[0].a + vertices[2].c;
    output.edges[0] = should_be_one.x;
    output.edges[1] = should_be_one.y;
    output.edges[2] = should_be_one.z;
    output.inside[0] = should_be_one.w;
}

[domain("tri")]
[outputcontrolpoints(3)]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant")]
data main(InputPatch<data, 3> input, uint i : SV_OutputControlPointID)
{
    data o = (data)0;
    o.position = input[i].position;
    o.a = input[i].a;
    o.b = input[i].b;
    o.c = input[i].c;
    return o;
}