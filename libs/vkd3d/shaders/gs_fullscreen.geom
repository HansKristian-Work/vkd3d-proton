#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in int i_layer[3];

void main() {
  for (int i = 0; i < 3; i++) {
    gl_Layer    = i_layer[i];
    gl_Position = gl_in[i].gl_Position;
    EmitVertex();
  }

  EndPrimitive();
}