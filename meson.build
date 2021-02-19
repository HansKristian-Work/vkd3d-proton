project('vkd3d-proton', ['c'], version : '2.2', meson_version : '>= 0.49', default_options : [
  'warning_level=2',
])

cpu_family = target_machine.cpu_family()

vkd3d_compiler = meson.get_compiler('c')
vkd3d_msvc     = vkd3d_compiler.get_id() == 'msvc'
vkd3d_clang    = vkd3d_compiler.get_id() == 'clang'
vkd3d_c_std    = 'c11'
vkd3d_platform = target_machine.system()

enable_tests             = get_option('enable_tests')
enable_extras            = get_option('enable_extras')
enable_d3d12             = get_option('enable_d3d12')
enable_profiling         = get_option('enable_profiling')
enable_renderdoc         = get_option('enable_renderdoc')
enable_descriptor_qa     = get_option('enable_descriptor_qa')

if enable_d3d12 == 'auto'
    enable_d3d12 = (vkd3d_platform == 'windows')
else
    enable_d3d12 = (enable_d3d12 == 'true')
endif

if vkd3d_platform != 'windows' and enable_d3d12
  error('Standalone D3D12 is only supported on Windows.')
endif

add_project_arguments('-D_GNU_SOURCE',   language : 'c')
add_project_arguments('-DPACKAGE_VERSION="' + meson.project_version() + '"',   language : 'c')

if vkd3d_platform == 'windows'
  add_project_arguments('-D_WIN32_WINNT=0x600', language : 'c')
endif

if enable_d3d12
  add_project_arguments('-DVKD3D_BUILD_STANDALONE_D3D12', language : 'c')
endif

if enable_profiling
  add_project_arguments('-DVKD3D_ENABLE_PROFILING', language : 'c')
endif

if enable_renderdoc
  add_project_arguments('-DVKD3D_ENABLE_RENDERDOC', language : 'c')
endif

if enable_descriptor_qa
  add_project_arguments('-DVKD3D_ENABLE_DESCRIPTOR_QA', language : 'c')
endif

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

glsl_compiler  = find_program('glslangValidator')
glsl_generator = generator(glsl_compiler,
  output    : [ '@BASENAME@.h' ],
  arguments : [ '--quiet', '-V', '--vn', '@BASENAME@', '@INPUT@', '-o', '@OUTPUT@' ])

threads_dep = dependency('threads')
lib_d3d12   = vkd3d_compiler.find_library('d3d12', required : false)

if vkd3d_platform == 'linux'
  lib_dl           = vkd3d_compiler.find_library('dl')
  vkd3d_extra_libs = [ lib_dl, threads_dep ]
elif vkd3d_platform == 'windows'
  lib_dxgi         = vkd3d_compiler.find_library('dxgi')
  vkd3d_extra_libs = [ threads_dep ]
else
  error('Unknown platform')
endif

add_project_arguments(vkd3d_compiler.get_supported_arguments([
    '-fvisibility=hidden',
    '-Wno-format',
    '-Wno-missing-field-initializers',
    '-Wno-unused-parameter',
    '-Wdeclaration-after-statement',
    '/wd4244', # Narrowing conversion
    '/wd4101', # Unreferenced local variable
    '/wd4267', # Another narrowing conversion
    '/wd4996', # Secure no warnings
    '/wd4334', # Result of 32-bit shift cast to 64-bit
    '/wd4146', # Unary minus on unsigned
    '/wd4305', # Truncation from double to float
    ]),
  language : 'c')

if cpu_family == 'x86'
  add_global_link_arguments(vkd3d_compiler.get_supported_link_arguments([
      '-Wl,--add-stdcall-alias',
      '-Wl,--enable-stdcall-fixup']),
    language : [ 'c', 'cpp' ])

  # Need to link against libatomic for 64-bit atomic emulation on x86
  # when using clang.
  if vkd3d_clang
    lib_atomic = vkd3d_compiler.find_library('atomic')
    vkd3d_extra_libs += lib_atomic
  endif
endif

vkd3d_build = vcs_tag(
  command : ['git', 'describe', '--always', '--exclude=*', '--abbrev=15', '--dirty=0'],
  input   : 'vkd3d_build.h.in',
  output  : 'vkd3d_build.h')

vkd3d_version = vcs_tag(
  command : ['git', 'describe', '--always', '--tags', '--dirty=+'],
  input   : 'vkd3d_version.h.in',
  output  : 'vkd3d_version.h')

dxil_spirv = subproject('dxil-spirv')
dxil_spirv_dep = dxil_spirv.get_variable('dxil_spirv_dep')

subdir('include')
subdir('libs')

if vkd3d_platform == 'linux'
  pkg = import('pkgconfig')
  pkg.generate(vkd3d_lib,       filebase : 'libvkd3d-proton',       subdirs : 'vkd3d-proton', description : 'The VKD3D-Proton 3D Graphics Library')
  pkg.generate(vkd3d_utils_lib, filebase : 'libvkd3d-proton-utils', subdirs : 'vkd3d-proton', description : 'The VKD3D-Proton 3D Graphics Utility Library')

  install_headers('include/vkd3d.h', 'include/vkd3d_sonames.h', 'include/vkd3d_types.h', 'include/vkd3d_utils.h', 'include/vkd3d_win32.h', 'include/vkd3d_windows.h', subdir : 'vkd3d-proton')
endif

if vkd3d_platform == 'windows'
  if enable_d3d12
    lib_d3d12 = d3d12_dep
  elif not lib_d3d12.found()
    lib_d3d12 = vkd3d_utils_dep
    if enable_extras
      warning('No d3d12 lib, falling back to vkd3d_utils for extras ...')
    endif
  endif
endif

if enable_tests
  subdir('tests')
endif

if enable_extras
  if vkd3d_platform == 'linux'
    lib_m            = vkd3d_compiler.find_library('m')
    lib_xcb          = vkd3d_compiler.find_library('xcb')
    lib_xcbkeysyms   = vkd3d_compiler.find_library('xcb-keysyms')
  endif

  subdir('demos')
  subdir('programs')
endif
