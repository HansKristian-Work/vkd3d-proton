vkd3d_hlsl_build_files = files(
  'dxbc_library.cpp',
  'dxil_library.cpp',
  'main.cpp',
)

hlsl_build_cpp_args = []

if not vkd3d_is_msvc
  hlsl_build_cpp_args += ['-Wno-cast-function-type']
endif

if vkd3d_platform == 'windows'
  add_languages('cpp', native : false, required : true)
  executable('vkd3d-hlsl-build', vkd3d_hlsl_build_files, vkd3d_header_files,
    include_directories : vkd3d_private_includes,
    cpp_args : hlsl_build_cpp_args,
    override_options: ['cpp_std=c++17'])
endif
