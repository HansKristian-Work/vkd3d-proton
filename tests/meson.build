
vkd3d_test_flags = []
vkd3d_test_deps = [ lib_d3d12, vkd3d_common_dep ]
if vkd3d_platform == 'windows'
  vkd3d_test_deps += [ lib_dxgi ]
endif

d3d12_test_utils_lib = static_library('d3d12-test-utils', 'd3d12_test_utils.c',
  dependencies        : vkd3d_test_deps,
  include_directories : vkd3d_private_includes)

d3d12_test_src = [
  'd3d12_clip_cull_distance.c',
  'd3d12_enhanced_barriers.c',
  'd3d12_sampler_feedback.c',
  'd3d12_geometry_shader.c',
  'd3d12_win32_exclusive.c',
  'd3d12_root_signature.c',
  'd3d12_depth_stencil.c',
  'd3d12_render_target.c',
  'd3d12_tessellation.c',
  'd3d12_sm_advanced.c',
  'd3d12_mesh_shader.c',
  'd3d12_descriptors.c',
  'd3d12_raytracing.c',
  'd3d12_robustness.c',
  'd3d12_streamout.c',
  'd3d12_pso_blob.c',
  'd3d12_resource.c',
  'd3d12_bindless.c',
  'd3d12_dstorage.c',
  'd3d12_shaders.c',
  'd3d12_command.c',
  'd3d12_sparse.c',
  'd3d12_device.c',
  'd3d12_clear.c',
  'd3d12_query.c',
  'd3d12_sync.c',
  'd3d12_copy.c',
  'd3d12_vrs.c',
  'd3d12_pso.c',
  'd3d12_rov.c',
  'd3d12_va.c',
  'd3d12.c',
]

executable('d3d12', d3d12_test_src,
  dependencies        : vkd3d_test_deps,
  include_directories : vkd3d_private_includes,
  install             : false,
  c_args              : vkd3d_test_flags,
  link_with           : [ d3d12_test_utils_lib ])

executable('descriptor-performance', 'descriptor_performance.c',
  dependencies        : vkd3d_test_deps,
  include_directories : vkd3d_private_includes,
  install             : false,
  c_args              : vkd3d_test_flags,
  link_with           : [ d3d12_test_utils_lib ])

executable('pso-library-bloat', 'pso_library_bloat.c',
  dependencies        : vkd3d_test_deps,
  include_directories : vkd3d_private_includes,
  install             : false,
  c_args              : vkd3d_test_flags,
  link_with           : [ d3d12_test_utils_lib ])

executable('shader-api', 'vkd3d_shader_api.c',
  dependencies        : [ vkd3d_test_deps, vkd3d_shader_dep ],
  include_directories : [ vkd3d_private_includes, vkd3d_shader_private_includes ],
  install             : false,
  c_args              : vkd3d_test_flags,
  link_with           : [ d3d12_test_utils_lib ])
