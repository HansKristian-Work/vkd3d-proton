ByteAddressBuffer b;

struct data
{
    float4 position : POSITION;
    float ref_buffer_data : REF_BUFFER_DATA;
};

struct ds_data
{
    float4 position : POSITION;
    float ref_buffer_data : REF_BUFFER_DATA;
    uint primitive_id : PRIM_ID;
    uint invocation_id : CP_ID;
};

struct ps_data
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct patch_constant_data
{
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
    float buffer_data : BUFFER_DATA;
    uint primitive_id : PATCH_PRIM_ID;
};

[domain("tri")]
void main(patch_constant_data input,
        float3 tess_coord : SV_DomainLocation,
        const OutputPatch<ds_data, 3> patch,
        out ps_data output)
{
    uint i;

    output.position = tess_coord.x * patch[0].position
            + tess_coord.y * patch[1].position
            + tess_coord.z * patch[2].position;

    for (i = 0; i < 3; ++i)
    {
        if (patch[i].ref_buffer_data != input.buffer_data)
        {
            output.color = float4(1, patch[i].ref_buffer_data / 255.0f, input.buffer_data / 255.0f, 0);
            return;
        }
    }

    for (i = 0; i < 3; ++i)
    {
        if (patch[i].primitive_id != input.primitive_id)
        {
            output.color = float4(1, 0, 1, 1);
            return;
        }
    }

    if (patch[0].invocation_id != 0 || patch[1].invocation_id != 1 || patch[2].invocation_id != 2)
    {
        output.color = float4(1, 1, 0, 1);
        return;
    }

    output.color = float4(0, 1, 0, 1);
}