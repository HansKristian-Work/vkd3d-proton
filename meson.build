project('vkd3d', ['c'], version : '1.1', meson_version : '>= 0.51')

cpu_family = target_machine.cpu_family()

add_project_arguments('-DHAVE_DXIL_SPV', language : 'c')
add_project_arguments('-D_GNU_SOURCE',   language : 'c')
add_project_arguments('-DPACKAGE_VERSION="' + meson.project_version() + '"',   language : 'c')

vkd3d_compiler = meson.get_compiler('c')
vkd3d_c_std    = 'c99'

vkd3d_external_includes = [ './subprojects/Vulkan-Headers/include', './subprojects/SPIRV-Headers/include' ]
vkd3d_public_includes   = [ './include' ] + vkd3d_external_includes
vkd3d_private_includes  = [ './include/private' ] + vkd3d_public_includes

vkd3d_external_includes = include_directories(vkd3d_external_includes)
vkd3d_public_includes   = include_directories(vkd3d_public_includes)
vkd3d_private_includes  = include_directories(vkd3d_private_includes)

idl_compiler  = find_program('widl')
idl_generator = generator(idl_compiler,
  output    : [ '@BASENAME@.h' ],
  arguments : [ '-h', '-o', '@OUTPUT@', '@INPUT@' ])

threads_dep = dependency('threads')

vkd3d_platform = target_machine.system()
if vkd3d_platform == 'linux'
  lib_dl           = vkd3d_compiler.find_library('dl')
  # For the demos...
  lib_m            = vkd3d_compiler.find_library('m')
  lib_xcb          = vkd3d_compiler.find_library('xcb')
  lib_xcbkeysyms   = vkd3d_compiler.find_library('xcb-keysyms')
  vkd3d_extra_libs = [ lib_dl, threads_dep ]
elif vkd3d_platform == 'windows'
  lib_dxgi         = vkd3d_compiler.find_library('dxgi')
  lib_d3d12        = vkd3d_compiler.find_library('d3d12', required : false)
  vkd3d_extra_libs = [ threads_dep ]
else
  error('Unknown platform')
endif

add_project_arguments(vkd3d_compiler.get_supported_arguments([
    '-Wno-format',
    '-Wno-discarded-qualifiers',
    '-Wno-incompatible-pointer-types',
    '-Wno-missing-braces']),
  language : 'c')

vkd3d_version = vcs_tag(
  command : ['git', 'describe', '--dirty=+'],
  input   : 'vkd3d_version.c.in',
  output  : 'vkd3d_version.c')

cmake = import('cmake')
dxil_spirv = cmake.subproject('dxil-spirv', cmake_options: ['-DDXIL_SPIRV_CLI=OFF'] )
dxil_spirv_lib = dxil_spirv.dependency('dxil-spirv-c-shared')

subdir('include')
subdir('libs')

enable_tests  = get_option('enable_tests')
enable_extras = get_option('enable_extras')

if enable_tests
  subdir('tests')
endif

if enable_extras
  if not lib_d3d12.found()
    lib_d3d12 = vkd3d_utils_dep
    warning('No d3d12 lib, falling back to vkd3d_utils for extras...')
  endif

  subdir('demos')
  subdir('programs')
endif