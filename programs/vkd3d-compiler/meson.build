executable('vkd3d-compiler', 'main.c',
  dependencies        : [ vkd3d_shader_dep, threads_dep ],
  include_directories : vkd3d_private_includes,
  install             : true,
  sources             : vkd3d_idl_h,
  override_options    : [ 'c_std='+vkd3d_c_std ])