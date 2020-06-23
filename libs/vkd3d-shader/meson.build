vkd3d_shader_src = [
  'checksum.c',
  'dxil.c',
  'dxbc.c',
  'spirv.c',
  'trace.c',
  'vkd3d_shader_main.c',
]

vkd3d_shader_lib = shared_library('vkd3d-shader', vkd3d_shader_src, vkd3d_headers,
  dependencies        : [ vkd3d_common_dep, dxil_spirv_lib ],
  include_directories : vkd3d_private_includes,
  install             : true,
  vs_module_defs      : 'vkd3d_shader.def',
  override_options    : [ 'c_std='+vkd3d_c_std ])

vkd3d_shader_dep = declare_dependency(
  link_with           : vkd3d_shader_lib,
  include_directories : vkd3d_public_includes)