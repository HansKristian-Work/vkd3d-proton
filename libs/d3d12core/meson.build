d3d12core_src = [
  'main.c'
]

if vkd3d_platform == 'windows'
  d3d12core_name_prefix = ''
  d3d12core_dependencies = [ vkd3d_dep, lib_dxgi ]
else
  d3d12core_name_prefix = 'libvkd3d-proton-'
  d3d12core_dependencies = [ vkd3d_dep, lib_dl ]
endif

d3d12core_needs_defs = (not vkd3d_is_msvc) and (vkd3d_platform == 'windows')

d3d12core_lib = shared_library('d3d12core', d3d12core_src,
  name_prefix         : d3d12core_name_prefix,
  dependencies        : d3d12core_dependencies,
  include_directories : vkd3d_private_includes,
  install             : true,
  objects             : d3d12core_needs_defs ? 'd3d12core.def' : [],
  vs_module_defs      : 'd3d12core.def',
  override_options    : [ 'c_std='+vkd3d_c_std ])

d3d12core_dep = declare_dependency(
  link_with           : d3d12core_lib,
  include_directories : vkd3d_public_includes)
