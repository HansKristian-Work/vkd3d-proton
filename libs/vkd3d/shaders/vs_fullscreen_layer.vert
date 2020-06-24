#version 450

#extension GL_ARB_shader_viewport_layer_array : enable

void main() {
  gl_Layer = gl_InstanceIndex;
  gl_Position = vec4(
    float(gl_VertexIndex & 1) * 4.0f - 1.0f,
    float(gl_VertexIndex & 2) * 2.0f - 1.0f,
    0.0f, 1.0f);
}