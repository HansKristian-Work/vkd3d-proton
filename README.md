# VKD3D

**Fork of VKD3D:** *The original project is available at: https://source.winehq.org/git/vkd3d.git/*

VKD3D is a 3D graphics library built on top of Vulkan. It has an API very
similar, but not identical, to Direct3D 12.

------

## Cloning the repo

To clone the repo you should run:
```
git clone --recursive https://github.com/HansKristian-Work/vkd3d
```
in order to pull in all the submodules which are needed for building.

## Building VKD3D

### Requirements:
- [wine](https://www.winehq.org/) (for `widl`)
  - On Windows this may be substituted for [Strawberry Perl](http://strawberryperl.com/) as it ships `widl` and is easy to find and install -- although this dependency may be eliminated in the future.
- [Meson](http://mesonbuild.com/) build system (at least version 0.51)
- [glslang](https://github.com/KhronosGroup/glslang) compiler

### Building:
#### The simple way
Inside the VKD3D directory, run:
```
./package-release.sh master /your/target/directory --no-package
```

This will create a folder `vkd3d-master` in `/your/target/directory`, which contains both 32-bit and 64-bit versions of VKD3D, which can be set up in the same way as the release versions as noted above.

In order to preserve the build directories for development, pass `--dev-build` to the script. This option implies `--no-package`. After making changes to the source code, you can then do the following to rebuild VKD3D:
```
# change to build.86 for 32-bit
cd /your/target/directory/build.64
ninja install
```

#### Compiling manually
```
# 64-bit build.
meson --buildtype release --prefix /your/vkd3d/directory build.64
cd build.64
ninja install

# 32-bit build
meson --cross-file x86-linux-gnu --buildtype release --prefix /your/vkd3d/directory build.86
cd build.86
ninja install
```

## Using VKD3D

VKD3D can be used by projects that target Direct3D 12 as a drop-in replacement
at build-time with some modest source modifications.

If VKD3D is available when building Wine, then Wine will use it to support
Direct3D 12 applications.

## Environment variables

Most of the environment variables used by VKD3D are for debugging purposes. The
environment variables are not considered a part of API and might be changed or
removed in the future versions of VKD3D.

Some of debug variables are lists of elements. Elements must be separated by
commas or semicolons.

 - `VKD3D_CONFIG` - a list of options that change the behavior of libvkd3d.
    - vk_debug - enables Vulkan debug extensions.
 - `VKD3D_DEBUG` - controls the debug level for log messages produced by
   libvkd3d. Accepts the following values: none, err, fixme, warn, trace.
 - `VKD3D_VULKAN_DEVICE` - a zero-based device index. Use to force the selected
   Vulkan device.
 - `VKD3D_DISABLE_EXTENSIONS` - a list of Vulkan extensions that libvkd3d should
   not use even if available.
 - `VKD3D_SHADER_DUMP_PATH` - path where shader bytecode is dumped.
 - `VKD3D_TEST_DEBUG` - enables additional debug messages in tests. Set to 0, 1
   or 2.
 - `VKD3D_TEST_FILTER` - a filter string. Only the tests whose names matches the
   filter string will be run, e.g. `VKD3D_TEST_FILTER=clear_render_target`.
   Useful for debugging or developing new tests.
 - `VKD3D_TEST_PLATFORM` - can be set to "wine", "windows" or "other". The test
   platform controls the behavior of todo(), todo_if(), bug_if() and broken()
   conditions in tests.
 - `VKD3D_TEST_BUG` - set to 0 to disable bug_if() conditions in tests.

