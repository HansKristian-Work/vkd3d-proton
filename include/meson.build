vkd3d_idl = [
  'vkd3d_d3d12.idl',
  'vkd3d_d3d12sdklayers.idl',
  'vkd3d_d3dcommon.idl',
  'vkd3d_dxcapi.idl',
  'vkd3d_dxgi.idl',
  'vkd3d_dxgi1_2.idl',
  'vkd3d_dxgi1_3.idl',
  'vkd3d_dxgi1_4.idl',
  'vkd3d_dxgi1_5.idl',
  'vkd3d_dxgibase.idl',
  'vkd3d_dxgiformat.idl',
  'vkd3d_dxgitype.idl',
  'vkd3d_swapchain_factory.idl',
  'vkd3d_command_list_vkd3d_ext.idl',
  'vkd3d_command_queue_vkd3d_ext.idl',
  'vkd3d_device_vkd3d_ext.idl',
  'vkd3d_core_interface.idl',
]

vkd3d_header_files = idl_generator.process(vkd3d_idl)

# Install generated IDL headers
foreach idl_file : vkd3d_idl
  header_name = idl_file.split('.')[0] + '.h'
  custom_target('install_' + header_name,
    input : idl_generator.process(idl_file),
    output : header_name,
    command : ['cp', '@INPUT@', '@OUTPUT@'],
    install : true,
    install_dir : get_option('includedir') / 'vkd3d-proton')
endforeach
