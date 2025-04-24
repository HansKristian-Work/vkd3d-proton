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

[domain("tri")]
void main(patch_constant_data input,
        float3 tess_coord : SV_DomainLocation,
        const OutputPatch<data, 3> patch,
        out float4 position : SV_Position,
        out float4 a : TEXCOORD0,
        out float4 b : TEXCOORD1,
        out float4 c : TEXCOORD2)
{
    position = patch[0].position * tess_coord.x +
        patch[1].position * tess_coord.y +
        patch[2].position * tess_coord.z;

    a = patch[0].a;
    b = patch[1].b;
    c = patch[2].c;
}