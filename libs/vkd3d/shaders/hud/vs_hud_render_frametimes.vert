#version 460

#extension GL_GOOGLE_include_directive : enable

#include "hud_common.glsl"

layout(push_constant, scalar)
uniform push_data_t
{
    hud_buffer_in   data_buffer;
    u32vec2         swapchain_extent;
    i32vec2         graph_location;
    u32vec2         graph_extent;
    uint32_t        frame_index;
};

layout(location = 0) out vec2 o_coord;

const uvec2 coord_mask = uvec2(0x2a, 0x1c);

void main()
{
    vec2 coord = vec2(
        float(gl_VertexIndex  & 1),
        float(gl_VertexIndex >> 1));

    o_coord = vec2(coord.x, 1.0f - coord.y);

    vec2 surface_size_f = vec2(swapchain_extent);

    vec2 pos = vec2(graph_location);
    vec2 size = vec2(graph_extent);

    pos = mix(pos, surface_size_f + pos, lessThan(pos, vec2(0.0f)));

    vec2 pixel_pos = pos + size * coord;
    vec2 scaled_pos = 2.0f * (pixel_pos / surface_size_f) - 1.0f;
    gl_Position = vec4(scaled_pos, 0.0f, 1.0f);
}
