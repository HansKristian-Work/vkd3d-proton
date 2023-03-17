d3d12_src = [
  'main.c'
]

if vkd3d_platform == 'windows'
  d3d12_name_prefix = ''
  d3d12_dependencies = [ vkd3d_dep, lib_dxgi ]
else
  d3d12_name_prefix = 'libvkd3d-proton-'
  # Link explicitly against d3d12core here so that we can alway find it
  # when just doing a `meson build` build, without it needing to be in
  # LD_LIBRARY_PATH.
  # We don't directly access any symbols in d3d12core, so d3d12.so will not actually link against d3d12core.so,
  # but dlopen will still find it since it is embedded in DT_RUNPATH (see objdump -p).
  # If the libraries are installed, the DT_RUNPATH is dropped, but at that point, LD_LIBRARY_PATH is more or less
  # expected.
  d3d12_dependencies = [ vkd3d_dep, lib_dl, d3d12core_dep ]
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
