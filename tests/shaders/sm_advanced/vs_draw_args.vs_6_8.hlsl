struct DrawInfo
{
    uint32_t draw_id;
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t base_vertex;
    uint32_t base_instance;
};

RWStructuredBuffer<uint> out_args : register(u0);
StructuredBuffer<DrawInfo> draw_args : register(t0);

cbuffer ShaderArgs : register(b0)
{
    uint draw_id;
};

float4 main(in uint base_vertex : SV_StartVertexLocation, in uint vertex_id : SV_VertexID,
    in uint base_instance : SV_StartInstanceLocation, in uint instance_id : SV_InstanceID) : SV_POSITION
{
    DrawInfo draw_info = draw_args[draw_id];

    uint match_mask = 0;

    if (base_instance == draw_info.base_instance && base_vertex == draw_info.base_vertex)
        match_mask |= 1u << (vertex_id + instance_id * draw_info.vertex_count);

    InterlockedOr(out_args[draw_id], match_mask);
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}
