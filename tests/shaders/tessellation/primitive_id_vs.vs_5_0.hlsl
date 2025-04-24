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

data main(in data input)
{
    return input;
}