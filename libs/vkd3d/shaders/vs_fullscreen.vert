#version 450

layout(location = 0) out int o_layer;

void main() {
  o_layer = gl_InstanceIndex;
  gl_Position = vec4(
    float(gl_VertexIndex & 1) * 4.0f - 1.0f,
    float(gl_VertexIndex & 2) * 2.0f - 1.0f,
    0.0f, 1.0f);
}