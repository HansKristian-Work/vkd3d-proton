vkd3d_common_src = [
  'debug.c',
  'memory.c',
  'utf8.c',
  'profiling.c',
  'string.c',
  'file_utils.c',
  'platform.c',
]

vkd3d_common_lib = static_library('vkd3d_common', vkd3d_common_src, vkd3d_header_files,
  include_directories : vkd3d_private_includes,
  dependencies        : vkd3d_extra_libs,
  override_options    : [ 'c_std='+vkd3d_c_std ])

vkd3d_common_dep = declare_dependency(
  link_with           : vkd3d_common_lib,
  include_directories : [ vkd3d_public_includes, vkd3d_common_lib.private_dir_include() ])
