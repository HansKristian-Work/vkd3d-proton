#version 460

#extension GL_GOOGLE_include_directive : enable

#include "hud_common.glsl"

layout(push_constant, scalar)
uniform push_data_t
{
    hud_buffer_in   data_buffer;
    u32vec2         swapchain_extent;
};

layout(location = 0) out vec2 o_texcoord;

layout(location = 1, component = 0) flat out vec3 o_color;
layout(location = 1, component = 3) flat out float o_falloff;

const uvec2 coord_mask = uvec2(0x2a, 0x1c);

void main()
{
    hud_text_draw_info_t draw_info = data_buffer.text.draw_infos[gl_InstanceIndex];

    o_color = unpackUnorm4x8(draw_info.color).bgr;
    o_falloff = float(bitfieldExtract(data_buffer.font.packed_sdf_falloff_mono_advance, 0, 8));

    float font_advance = float(bitfieldExtract(data_buffer.font.packed_sdf_falloff_mono_advance, 8, 8));

    /* Compute character index and vertex index for the current
     * character. We'll render two triangles per character. */
    uint chr_idx = gl_VertexIndex / 6u;
    uint vtx_idx = gl_VertexIndex - 6u * chr_idx;

    /* Load glyph info based on vertex index */
    uint dword_index = (draw_info.text_offset + chr_idx) / 4u;
    uint dword_byte = (draw_info.text_offset + chr_idx) % 4u;

    uint glyph_idx = bitfieldExtract(data_buffer.text.text_as_dwords[dword_index], int(8u * dword_byte), 8);

    if (glyph_idx >= HUD_FONT_MAX_GLYPHS)
        glyph_idx = 0u;

    hud_glyph_t glyph_info = data_buffer.font.glyphs[glyph_idx];

    /* Compute texture coordinate from glyph data */
    vec2 vertex_coord = vec2((coord_mask >> vtx_idx) & 0x1);

    vec2 tex_xy = vec2(glyph_info.texture_location);
    vec2 tex_wh = vec2(
        bitfieldExtract(glyph_info.packed_texture_size, 0, 8),
        bitfieldExtract(glyph_info.packed_texture_size, 8, 8));

    vec2 glyph_origin = vec2(
        bitfieldExtract(glyph_info.packed_origin, 0, 8),
        bitfieldExtract(glyph_info.packed_origin, 8, 8));

    o_texcoord = tex_xy + vertex_coord * tex_wh;

    /* Compute actual vertex position */
    float size_factor = float(draw_info.size) / float(data_buffer.font.font_size);
    vec2 surface_size_f = vec2(swapchain_extent);

    vec2 text_pos = vec2(draw_info.location);
    text_pos = mix(text_pos, surface_size_f + text_pos, lessThan(text_pos, vec2(0.0f)));

    vec2 local_pos = tex_wh * vertex_coord - glyph_origin + vec2(font_advance * float(chr_idx), 0.0f);
    vec2 pixel_pos = text_pos + size_factor * local_pos;
    vec2 scaled_pos = 2.0f * (pixel_pos / surface_size_f) - 1.0f;

    gl_Position = vec4(scaled_pos, 0.0f, 1.0f);
}
