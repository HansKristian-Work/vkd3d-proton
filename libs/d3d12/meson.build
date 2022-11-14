d3d12_src = [
  'main.c'
]

if vkd3d_platform == 'windows'
  d3d12_name_prefix = ''
  d3d12_dependencies = [ vkd3d_dep, lib_dxgi ]
else
  d3d12_name_prefix = 'libvkd3d-proton-'
  d3d12_dependencies = [ vkd3d_dep, lib_dl ]
endif

d3d12_needs_defs = (not vkd3d_is_msvc) and (vkd3d_platform == 'windows')

d3d12_lib = shared_library('d3d12', d3d12_src,
  name_prefix         : d3d12_name_prefix,
  dependencies        : d3d12_dependencies,
  include_directories : vkd3d_private_includes,
  install             : true,
  objects             : d3d12_needs_defs ? 'd3d12.def' : [],
  vs_module_defs      : 'd3d12.def',
  override_options    : [ 'c_std='+vkd3d_c_std ])

d3d12_dep = declare_dependency(
  link_with           : d3d12_lib,
  include_directories : vkd3d_public_includes)
