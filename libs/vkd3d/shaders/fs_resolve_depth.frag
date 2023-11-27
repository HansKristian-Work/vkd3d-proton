#version 450

#extension GL_EXT_samplerless_texture_functions : enable

#define MODE_MIN 1
#define MODE_MAX 2
#define MODE_AVERAGE 3

layout(constant_id = 0) const uint c_mode = 0;

layout(binding = 0) uniform texture2DMSArray tex_ms;

layout(push_constant)
uniform u_info_t {
  ivec2 offset;
} u_info;

void main() {
  ivec3 coord = ivec3(u_info.offset + ivec2(gl_FragCoord.xy), gl_Layer);

  uint samples = textureSamples(tex_ms);
  float depth = texelFetch(tex_ms, coord, 0).x;

  for (uint i = 1; i < samples; i++) {
    float sample_value = texelFetch(tex_ms, coord, int(i)).x;

    switch (c_mode) {
      case MODE_MIN: depth = min(depth, sample_value); break;
      case MODE_MAX: depth = max(depth, sample_value); break;
      case MODE_AVERAGE: depth += sample_value; break;
    }
  }

  if (c_mode == MODE_AVERAGE)
    depth /= float(samples);

  gl_FragDepth = depth;
}
