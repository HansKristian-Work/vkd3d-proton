struct data
{
    float4 position : SV_Position;
};

struct patch_constant_data
{
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

[domain("tri")]
void main(patch_constant_data input,
        float3 tess_coord : SV_DomainLocation,
        const OutputPatch<data, 3> patch,
        out data output)
{
    output.position = tess_coord.x * patch[0].position
            + tess_coord.y * patch[1].position
            + tess_coord.z * patch[2].position;
}
