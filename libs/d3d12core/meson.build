d3d12core_src = [
  'main.c'
]

d3d12core_needs_defs = (not vkd3d_is_msvc) and (vkd3d_platform == 'windows')

d3d12core_lib = shared_library('d3d12core', d3d12core_src,
  name_prefix         : '',
  install             : true,
  objects             : d3d12core_needs_defs ? 'd3d12core.def' : [],
  vs_module_defs      : 'd3d12core.def',
  override_options    : [ 'c_std='+vkd3d_c_std ])

