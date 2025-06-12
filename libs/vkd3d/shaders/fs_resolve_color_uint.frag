#version 450

#extension GL_EXT_samplerless_texture_functions : enable

#define MODE_MIN 1
#define MODE_MAX 2

layout(constant_id = 0) const uint c_mode = 0;

layout(binding = 0) uniform utexture2DMSArray tex_ms;

layout(location = 0) out uvec4 o_color;

layout(push_constant)
uniform u_info_t {
  ivec2 offset;
} u_info;

void main() {
  ivec3 coord = ivec3(u_info.offset + ivec2(gl_FragCoord.xy), gl_Layer);

  uint samples = textureSamples(tex_ms);
  uvec4 color = texelFetch(tex_ms, coord, 0);

  for (uint i = 1; i < samples; i++) {
    uvec4 sample_value = texelFetch(tex_ms, coord, int(i));

    switch (c_mode) {
      case MODE_MIN: color = min(color, sample_value); break;
      case MODE_MAX: color = max(color, sample_value); break;
    }
  }

  o_color = color;
}
