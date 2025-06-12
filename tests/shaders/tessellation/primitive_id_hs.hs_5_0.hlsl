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

void patch_constant(uint prim_id : SV_PrimitiveID, out patch_constant_data output)
{
    output.edges[0] = output.edges[1] = output.edges[2] = 4.0f;
    output.inside = 4.0f;
    output.buffer_data = b.Load(4 * prim_id);
    output.primitive_id = prim_id;
}

[domain("tri")]
[outputcontrolpoints(3)]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant")]
ds_data main(const InputPatch<data, 3> input,
        uint prim_id : SV_PrimitiveID, uint i : SV_OutputControlPointID)
{
    ds_data output;
    output.position = input[i].position;
    output.ref_buffer_data = input[i].ref_buffer_data;
    output.primitive_id = prim_id;
    output.invocation_id = i;
    return output;
}