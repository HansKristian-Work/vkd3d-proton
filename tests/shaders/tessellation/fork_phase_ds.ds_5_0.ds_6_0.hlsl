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

[domain("quad")]
void main(patch_constant_data input,
        float2 tess_coord : SV_DomainLocation,
        const OutputPatch<data, 4> patch,
        out float4 position : SV_Position,
        out float4 color : COLOR)
{
    float4 a = lerp(patch[0].position, patch[1].position, tess_coord.x);
    float4 b = lerp(patch[2].position, patch[3].position, tess_coord.x);
    position = lerp(a, b, tess_coord.y);

    color = float4(input.center, 1.0);
}