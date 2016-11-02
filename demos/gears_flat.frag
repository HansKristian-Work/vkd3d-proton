#version 150
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) flat in vec4 colour_in;
layout(location = 0) out vec4 colour_out;

void main(void)
{
    colour_out = colour_in;
}
