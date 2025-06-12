vkd3d_shaders =[
  'shaders/fs_empty.frag',

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
  'shaders/cs_emit_nv_memory_decompression_regions.comp',
  'shaders/cs_emit_nv_memory_decompression_workgroups.comp',
  'shaders/cs_predicate_command.comp',
  'shaders/cs_predicate_command_execute_indirect.comp',
  'shaders/cs_resolve_binary_queries.comp',
  'shaders/cs_resolve_predicate.comp',
  'shaders/cs_resolve_query.comp',

  'shaders/fs_copy_image_float.frag',
  'shaders/fs_copy_image_uint.frag',
  'shaders/fs_copy_image_stencil.frag',
  'shaders/fs_copy_image_stencil_no_export.frag',

  'shaders/gs_fullscreen.geom',
  'shaders/vs_fullscreen.vert',
  'shaders/vs_fullscreen_layer.vert',

  'shaders/vs_swapchain_fullscreen.vert',
  'shaders/fs_swapchain_fullscreen.frag',
  'shaders/cs_execute_indirect_patch.comp',
  'shaders/cs_execute_indirect_patch_debug_ring.comp',
  'shaders/cs_execute_indirect_multi_dispatch.comp',
  'shaders/cs_execute_indirect_multi_dispatch_state.comp',

  'shaders/cs_sampler_feedback_decode_buffer_min_mip.comp',
  'shaders/fs_sampler_feedback_decode_image_min_mip.frag',
  'shaders/fs_sampler_feedback_decode_image_mip_used.frag',
  'shaders/cs_sampler_feedback_encode_buffer_min_mip.comp',
  'shaders/cs_sampler_feedback_encode_image_min_mip.comp',
  'shaders/cs_sampler_feedback_encode_image_mip_used.comp',

  'shaders/fs_resolve_color_float.frag',
  'shaders/fs_resolve_color_uint.frag',
  'shaders/fs_resolve_color_sint.frag',
  'shaders/fs_resolve_depth.frag',
  'shaders/fs_resolve_stencil.frag',
  'shaders/fs_resolve_stencil_no_export.frag',

  'shaders/cs_resolve_color_float.comp',
  'shaders/cs_resolve_color_uint.comp',
  'shaders/cs_resolve_color_sint.comp',

  'shaders/cs_workgraph_distribute_workgroups.comp',
  'shaders/cs_workgraph_distribute_payload_offsets.comp',
  'shaders/cs_workgraph_complete_compaction.comp',
  'shaders/cs_workgraph_setup_gpu_input.comp',

  'shaders/cs_gdeflate.comp',
  'shaders/cs_gdeflate_prepare.comp',
]

vkd3d_src = [
  'bundle.c',
  'cache.c',
  'command.c',
  'command_list_vkd3d_ext.c',
  'command_queue_vkd3d_ext.c',
  'device.c',
  'device_vkd3d_ext.c',
  'heap.c',
  'memory.c',
  'meta.c',
  'meta_commands.c',
  'resource.c',
  'state.c',
  'utils.c',
  'debug_ring.c',
  'va_map.c',
  'vkd3d_main.c',
  'raytracing_pipeline.c',
  'state_object_common.c',
  'acceleration_structure.c',
  'swapchain.c',
  'queue_timeline.c',
  'address_binding_tracker.c'
]

if enable_renderdoc
  vkd3d_src += ['renderdoc.c']
endif

if enable_descriptor_qa
  vkd3d_src += ['descriptor_debug.c']
endif

if enable_breadcrumbs
  vkd3d_src += ['breadcrumbs.c']
endif

if enable_descriptor_qa or enable_breadcrumbs
  vkd3d_src += ['debug_common.c']
endif

if vkd3d_platform == 'windows'
  vkd3d_src += ['shared_metadata.c']
endif

vkd3d_lib = static_library('vkd3d-proton', vkd3d_src, glsl_generator.process(vkd3d_shaders), vkd3d_build, vkd3d_version,
  dependencies        : [ vkd3d_common_dep, vkd3d_shader_dep ] + vkd3d_extra_libs,
  include_directories : vkd3d_private_includes)

vkd3d_dep = declare_dependency(
  link_with           : [ vkd3d_lib, vkd3d_common_lib ],
  dependencies        : [ vkd3d_extra_libs, vkd3d_common_dep ],
  include_directories : vkd3d_public_includes)
