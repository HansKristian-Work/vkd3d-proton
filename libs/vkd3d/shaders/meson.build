vkd3d_shaders = [
  'cs_clear_uav_buffer_float.comp',
  'cs_clear_uav_buffer_uint.comp',
  'cs_clear_uav_image_1d_array_float.comp',
  'cs_clear_uav_image_1d_array_uint.comp',
  'cs_clear_uav_image_1d_float.comp',
  'cs_clear_uav_image_1d_uint.comp',
  'cs_clear_uav_image_2d_array_float.comp',
  'cs_clear_uav_image_2d_array_uint.comp',
  'cs_clear_uav_image_2d_float.comp',
  'cs_clear_uav_image_2d_uint.comp',
  'cs_clear_uav_image_3d_float.comp',
  'cs_clear_uav_image_3d_uint.comp',

  'fs_copy_image_float.frag',

  'gs_fullscreen.geom',
  'vs_fullscreen.vert',
  'vs_fullscreen_layer.vert',
]

vkd3d_shader_h = []
vkd3d_shaders_h = '['
vkd3d_shaders_h += '\'#define VKD3D_META_COPY_MODE_1D (0)\','
vkd3d_shaders_h += '\'#define VKD3D_META_COPY_MODE_2D (1)\','
vkd3d_shaders_h += '\'#define VKD3D_META_COPY_MODE_MS (2)\','

foreach shader : vkd3d_shaders
  vkd3d_shader_h += custom_target('@0@.target'.format(shader),
    input: shader,
    output: '@BASENAME@.h',
    command: [ glsl_compiler, '-V', '-Os', '--vn', '@BASENAME@', '@INPUT@', '-o', '@OUTPUT@' ],
    build_by_default: true,
    install: false)

  vkd3d_shaders_h += '\'#include "@0@.h"\','.format(shader.split('/')[-1].split('.')[-2])
endforeach
vkd3d_shaders_h += ']'

# Don't ask me ...
prog_python = import('python').find_installation('python3')
prog_python_script = 'import os;open("@OUTPUT@","w").writelines(map(lambda x:x+os.linesep, @0@))'.format(vkd3d_shaders_h)

vkd3d_shaders_h = custom_target('vkd3d_shaders.h.target',
    output: 'vkd3d_shaders.h',
    command: [ prog_python, '-c', prog_python_script ],
    build_by_default: true,
    install: false)
