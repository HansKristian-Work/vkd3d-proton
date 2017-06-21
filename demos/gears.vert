#version 150
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 position_in;
layout(location = 1) in vec3 normal_in;
layout(location = 2) in vec3 diffuse_in;
layout(location = 3) in vec4 transform_in;

layout(location = 0) out vec4 colour_out;

uniform gear_block
{
    uniform mat4 mvp_matrix;
    uniform mat3 normal_matrix;
} gear;

void main()
{
    const vec3 l_pos = vec3(5.0, 5.0, 10.0);
    vec3 dir, normal;
    vec4 position;
    float att;

    position.x = transform_in.x * position_in.x - transform_in.y * position_in.y + transform_in.z;
    position.y = transform_in.x * position_in.y + transform_in.y * position_in.x + transform_in.w;
    position.zw = position_in.zw;

    gl_Position = gear.mvp_matrix * position;
    dir = normalize(l_pos - gl_Position.xyz / gl_Position.w);

    normal.x = transform_in.x * normal_in.x - transform_in.y * normal_in.y;
    normal.y = transform_in.x * normal_in.y + transform_in.y * normal_in.x;
    normal.z = normal_in.z;
    att = 0.2 + dot(dir, normalize(gear.normal_matrix * normal));

    colour_out.xyz = diffuse_in.xyz * att;
    colour_out.w = 1.0;
}
