vkd3d_shaders =[
  'shaders/cs_clear_uav_buffer_float.comp',
  'shaders/cs_clear_uav_buffer_raw.comp',
  'shaders/cs_clear_uav_buffer_uint.comp',
  'shaders/cs_clear_uav_image_1d_array_float.comp',
  'shaders/cs_clear_uav_image_1d_array_uint.comp',
  'shaders/cs_clear_uav_image_1d_float.comp',
  'shaders/cs_clear_uav_image_1d_uint.comp',
  'shaders/cs_clear_uav_image_2d_array_float.comp',
  'shaders/cs_clear_uav_image_2d_array_uint.comp',
  'shaders/cs_clear_uav_image_2d_float.comp',
  'shaders/cs_clear_uav_image_2d_uint.comp',
  'shaders/cs_clear_uav_image_3d_float.comp',
  'shaders/cs_clear_uav_image_3d_uint.comp',

  'shaders/fs_copy_image_float.frag',

  'shaders/gs_fullscreen.geom',
  'shaders/vs_fullscreen.vert',
  'shaders/vs_fullscreen_layer.vert',

  'shaders/vs_swapchain_fullscreen.vert',
  'shaders/fs_swapchain_fullscreen.frag',
]

vkd3d_src = [
  'cache.c',
  'command.c',
  'device.c',
  'meta.c',
  'platform.c',
  'resource.c',
  'state.c',
  'utils.c',
  'debug_ring.c',
  'vkd3d_main.c',
]

if enable_standalone_d3d12
  vkd3d_src += 'swapchain.c'
endif

if enable_renderdoc
  vkd3d_src += ['renderdoc.c']
endif

if not enable_standalone_d3d12
  vkd3d_lib = shared_library('vkd3d', vkd3d_src, glsl_generator.process(vkd3d_shaders), vkd3d_build, vkd3d_version, vkd3d_headers,
    dependencies        : [ vkd3d_common_dep, vkd3d_shader_dep ] + vkd3d_extra_libs,
    include_directories : vkd3d_private_includes,
    install             : true,
    version             : '1.1.0',
    c_args              : '-DVKD3D_EXPORTS',
    override_options    : [ 'c_std='+vkd3d_c_std ])
else
  vkd3d_lib = static_library('vkd3d', vkd3d_src, glsl_generator.process(vkd3d_shaders), vkd3d_build, vkd3d_version, vkd3d_headers,
    dependencies        : [ vkd3d_common_dep, vkd3d_shader_dep ] + vkd3d_extra_libs,
    include_directories : vkd3d_private_includes,
    override_options    : [ 'c_std='+vkd3d_c_std ])
endif

vkd3d_dep = declare_dependency(
  link_with           : [ vkd3d_lib, vkd3d_common_lib ],
  dependencies        : vkd3d_extra_libs,
  include_directories : vkd3d_public_includes)
