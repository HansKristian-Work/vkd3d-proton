#version 450

#extension GL_EXT_samplerless_texture_functions : enable
#extension GL_ARB_shader_stencil_export : enable

#define MODE_1D 0
#define MODE_2D 1
#define MODE_MS 2

layout(constant_id = 0) const uint c_mode = MODE_2D;

layout(binding = 0) uniform utexture1DArray tex_1d;
layout(binding = 0) uniform utexture2DArray tex_2d;
layout(binding = 0) uniform utexture2DMSArray tex_ms;

layout(push_constant)
uniform u_info_t {
  ivec2 offset;
} u_info;

void main() {
  ivec3 coord = ivec3(u_info.offset + ivec2(gl_FragCoord.xy), gl_Layer);
  uint value;
  if (c_mode == MODE_1D) value = texelFetch(tex_1d, coord.xz, 0).r;
  if (c_mode == MODE_2D) value = texelFetch(tex_2d, coord, 0).r;
  if (c_mode == MODE_MS) value = texelFetch(tex_ms, coord, gl_SampleID).r;
  gl_FragStencilRefARB = int(value);
}
