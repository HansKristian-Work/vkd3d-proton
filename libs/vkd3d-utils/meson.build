vkd3d_utils_src = [
  'vkd3d_utils_main.c',
]

vkd3d_utils_lib = shared_library('vkd3d-utils', vkd3d_utils_src, vkd3d_headers,
  dependencies        : vkd3d_dep,
  include_directories : vkd3d_private_includes,
  install             : true,
  c_args              : '-DVKD3D_UTILS_EXPORTS',
  override_options    : [ 'c_std='+vkd3d_c_std ])

vkd3d_utils_dep = declare_dependency(
  link_with           : vkd3d_utils_lib,
  include_directories : vkd3d_public_includes)