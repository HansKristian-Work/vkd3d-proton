vkd3d_src = [
  'command.c',
  'device.c',
  'meta.c',
  'platform.c',
  'resource.c',
  'state.c',
  'utils.c',
  'vkd3d_main.c',
]

vkd3d_lib = shared_library('vkd3d', vkd3d_src, vkd3d_version, vkd3d_headers,
  dependencies        : [ vkd3d_common_dep, vkd3d_shader_dep ] + vkd3d_extra_libs,
  include_directories : vkd3d_private_includes,
  install             : true,
  vs_module_defs      : 'vkd3d.def',
  version             : '1.1.0',
  override_options    : [ 'c_std='+vkd3d_c_std ])

vkd3d_dep = declare_dependency(
  link_with           : [ vkd3d_lib, vkd3d_common_lib ],
  dependencies        : vkd3d_extra_libs,
  include_directories : vkd3d_public_includes)
