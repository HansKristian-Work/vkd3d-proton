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

[domain("isoline")]
void main(patch_constant_data input,
        float tess_factor[2] : SV_TessFactor,
        float2 tess_coord : SV_DomainLocation,
        float3 a : A,
        float3 b : B,
        const OutputPatch<data, 1> patch,
        out float4 position : SV_Position,
        out float4 out_color : COLOR)
{
    position = float4(patch[0].position.xy, tess_coord);
    out_color = float4(0.5 * (a.xy + b.yz), tess_factor[0], tess_factor[1]);
}