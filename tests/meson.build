
if vkd3d_platform == 'windows'
  vkd3d_test_deps = [ lib_d3d12, lib_dxgi ]
else
  vkd3d_test_deps = [ vkd3d_dep, vkd3d_utils_dep ]
endif

executable('d3d12', 'd3d12.c', vkd3d_headers,
  dependencies        : vkd3d_test_deps,
  include_directories : vkd3d_private_includes,
  install             : true,
  override_options    : [ 'c_std='+vkd3d_c_std ])

executable('descriptor-performance', 'descriptor_performance.c', vkd3d_headers,
  dependencies        : vkd3d_test_deps,
  include_directories : vkd3d_private_includes,
  install             : true,
  override_options    : [ 'c_std='+vkd3d_c_std ])
