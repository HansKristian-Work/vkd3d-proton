executable('vkd3d-compiler', 'main.c', vkd3d_headers,
  dependencies        : [ vkd3d_shader_dep, threads_dep ],
  include_directories : vkd3d_private_includes,
  install             : true,
  override_options    : [ 'c_std='+vkd3d_c_std ])