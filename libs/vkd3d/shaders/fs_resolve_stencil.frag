#version 450

#extension GL_EXT_samplerless_texture_functions : enable
#extension GL_ARB_shader_stencil_export : enable

#define MODE_MIN 1
#define MODE_MAX 2

layout(constant_id = 0) const uint c_mode = 0;

layout(binding = 0) uniform utexture2DMSArray tex_ms;

layout(push_constant)
uniform u_info_t {
  ivec2 offset;
} u_info;

void main() {
  ivec3 coord = ivec3(u_info.offset + ivec2(gl_FragCoord.xy), gl_Layer);

  uint samples = textureSamples(tex_ms);
  uint stencil = texelFetch(tex_ms, coord, 0).x;

  for (uint i = 1; i < samples; i++) {
    uint sample_value = texelFetch(tex_ms, coord, int(i)).x;

    switch (c_mode) {
      case MODE_MIN: stencil = min(stencil, sample_value); break;
      case MODE_MAX: stencil = max(stencil, sample_value); break;
    }
  }

  gl_FragStencilRefARB = int(stencil);
}
