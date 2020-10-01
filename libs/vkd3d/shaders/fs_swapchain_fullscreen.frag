#version 450

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2D Tex;

void main()
{
  FragColor = textureLod(Tex, vUV, 0.0);
}
