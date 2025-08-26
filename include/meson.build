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

# idl_generator.process() returns GeneratedList
# even when called with single item so we process
# each idl file in a loop generating and installing
# them to avoid calling process() twice
vkd3d_header_files = []
foreach idl_file : vkd3d_idl
  header_file = idl_generator.process(idl_file)
  vkd3d_header_files += header_file

  header_name = idl_file.split('.')[0] + '.h'
  custom_target('install_' + header_name,
    input : header_file,
    output : header_name,
    command : ['cp', '@INPUT@', '@OUTPUT@'],
    install : true,
    install_dir : get_option('includedir') / 'vkd3d-proton')
endforeach
