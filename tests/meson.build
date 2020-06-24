vkd3d_test_deps = [ vkd3d_dep, vkd3d_utils_dep ]

executable('d3d12', 'd3d12.c', vkd3d_headers,
  dependencies        : vkd3d_test_deps + [ vkd3d_shader_dep ],
  include_directories : vkd3d_private_includes,
  install             : true,
  override_options    : [ 'c_std='+vkd3d_c_std ])

executable('d3d12_invalid_usage', 'd3d12_invalid_usage.c', vkd3d_headers,
  dependencies        : vkd3d_test_deps + [ vkd3d_shader_dep ],
  include_directories : vkd3d_private_includes,
  install             : true,
  override_options    : [ 'c_std='+vkd3d_c_std ])