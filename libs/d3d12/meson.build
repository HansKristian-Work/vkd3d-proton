d3d12_src = [
  'main.c'
]

d3d12_lib = shared_library('d3d12', d3d12_src,
  name_prefix         : '', # libd3d12.dll -> d3d12.dll
  dependencies        : [ vkd3d_dep, lib_dxgi ],
  include_directories : vkd3d_private_includes,
  install             : true,
  objects             : not vkd3d_msvc ? 'd3d12.def' : [],
  vs_module_defs      : 'd3d12.def',
  sources             : vkd3d_idl_h,
  override_options    : [ 'c_std='+vkd3d_c_std ])

d3d12_dep = declare_dependency(
  link_with           : d3d12_lib,
  include_directories : vkd3d_public_includes)
