demo_vkd3d_deps = [
  threads_dep
]

if vkd3d_platform != 'windows'
  demo_vkd3d_deps += [
    lib_m,
    lib_xcb,
    lib_xcbkeysyms,

    vkd3d_dep,
    vkd3d_utils_dep,
  ]
else
  demo_vkd3d_deps += [
    lib_dxgi,
    lib_d3d12
  ]
endif

executable('gears', 'gears.c', vkd3d_headers,
  dependencies        : demo_vkd3d_deps,
  include_directories : vkd3d_public_includes,
  install             : true,
  gui_app             : true,
  override_options    : [ 'c_std='+vkd3d_c_std ])

executable('triangle', 'triangle.c', vkd3d_headers,
  dependencies        : demo_vkd3d_deps,
  include_directories : vkd3d_public_includes,
  install             : true,
  gui_app             : true,
  override_options    : [ 'c_std='+vkd3d_c_std ])