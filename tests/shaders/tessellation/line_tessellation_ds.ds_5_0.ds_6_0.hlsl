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

[domain("isoline")]
void main(patch_constant_data input,
        // float tess_factor[2] : SV_TessFactor, DXC refused to compile, register overlap
        float2 tess_coord : SV_DomainLocation,
        //float3 color : COLOR, DXC refused to compile, register overlap
        //uint prim_id : PRIMITIVE_ID, DXC refused to compile, register overlap
        const OutputPatch<data, 1> patch,
        out float4 position : SV_Position,
        out float4 out_color : COLOR,
        out float4 out_prim_id : PRIMITIVE_ID)
{
    position = patch[0].position;
    out_color = float4(input.color, 1.0); // input.color on DXC
    out_prim_id = input.prim_id; // input.prim_id on DXC
}