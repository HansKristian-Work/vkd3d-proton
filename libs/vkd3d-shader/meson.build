vkd3d_shader_src = [
  'checksum.c',
  'dxil.c',
  'dxbc.c',
  'spirv.c',
  'vkd3d_shader_main.c',
  '3rdparty/md5/md5.c',
]

vkd3d_shader_lib = static_library('vkd3d-shader', vkd3d_shader_src,
  dependencies        : [ vkd3d_common_dep, dxil_spirv_dep ],
  include_directories : vkd3d_private_includes)

vkd3d_shader_dep = declare_dependency(
  link_with           : vkd3d_shader_lib,
  dependencies        : vkd3d_common_dep,
  include_directories : vkd3d_public_includes)
