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

[domain("tri")]
void main(patch_constant_data input,
    float3 tess_coord : SV_DomainLocation,
    const OutputPatch<hs_output, 3> patch,
    out hs_output output)
{
    output.position = tess_coord.x * patch[0].position * patch[0].clip
            + tess_coord.y * patch[1].position * patch[1].clip
            + tess_coord.z * patch[2].position * patch[2].clip;
    output.clip = 1.0;
}
