vkd3d_utils_src = [
  'vkd3d_utils_main.c',
]

vkd3d_utils_lib = shared_library('vkd3d-proton-utils', vkd3d_utils_src,
  dependencies        : vkd3d_dep,
  include_directories : vkd3d_private_includes,
  install             : true,
  objects             : not vkd3d_is_msvc and vkd3d_platform == 'windows'
                        ? 'vkd3d-proton-utils.def'
                        : [],
  vs_module_defs      : 'vkd3d-proton-utils.def',
  version             : '2.0.0',
  c_args              : '-DVKD3D_UTILS_EXPORTS',
  override_options    : [ 'c_std='+vkd3d_c_std ])

vkd3d_utils_dep = declare_dependency(
  link_with           : vkd3d_utils_lib,
  include_directories : vkd3d_public_includes)
