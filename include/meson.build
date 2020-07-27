vkd3d_idl = [
  'vkd3d_d3d12.idl',
  'vkd3d_d3d12sdklayers.idl',
  'vkd3d_d3dcommon.idl',
  'vkd3d_dxgi.idl',
  'vkd3d_dxgi1_2.idl',
  'vkd3d_dxgi1_3.idl',
  'vkd3d_dxgi1_4.idl',
  'vkd3d_dxgibase.idl',
  'vkd3d_dxgiformat.idl',
  'vkd3d_dxgitype.idl',
  'vkd3d_swapchain_factory.idl',
]

vkd3d_idl_h = []

foreach header : vkd3d_idl
  vkd3d_idl_h += custom_target( '@0@.target'.format(header),
    input: header,
    output: '@BASENAME@.h',
    command: [ idl_compiler, vkd3d_arch_arg, '-h', '-o', '@OUTPUT@', '@INPUT@' ])
endforeach
