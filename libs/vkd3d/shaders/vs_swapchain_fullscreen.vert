#version 450

layout(location = 0) out vec2 vUV;

void main()
{
  gl_Position = vec4(
    float(gl_VertexIndex & 1) * 4.0f - 1.0f,
    float(gl_VertexIndex & 2) * 2.0f - 1.0f,
    0.0f, 1.0f);
  vUV = gl_Position.xy * 0.5 + 0.5;
}
