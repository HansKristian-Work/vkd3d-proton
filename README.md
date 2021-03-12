# VKD3D-Proton

VKD3D-Proton is a fork of VKD3D, which aims to implement the full Direct3D 12 API on top of Vulkan.
The project serves as the development effort for Direct3D 12 support in [Proton](https://github.com/ValveSoftware/Proton).

## Upstream

The original project is available at [WineHQ](https://source.winehq.org/git/vkd3d.git/).

## Priorities

Performance and game compatibility are important targets, at the expense of compatibility with older drivers and systems.
Modern Vulkan extensions and features are aggressively made use of to improve game performance and compatibility.
It is recommended to use the very latest drivers you can get your hands on for the best experience.
Backwards compatibility with the vkd3d standalone API is not a goal of this project.

## Drivers

There are some hard requirements on drivers to be able to implement D3D12 in a reasonably performant way.

- Vulkan 1.1
- `VK_EXT_descriptor_indexing` with at least 1000000 UpdateAfterBind descriptors for all types except UniformBuffer.
  Essentially all features in `VkPhysicalDeviceDescriptorIndexingFeatures` must be supported.
- `VK_KHR_timeline_semaphore`

Some notable extensions that **should** be supported for optimal or correct behavior.
These extensions will likely become mandatory later.

- `VK_EXT_robustness2`
- `VK_KHR_buffer_device_address`
- `VK_EXT_extended_dynamic_state`

`VK_VALVE_mutable_descriptor_type` is also highly recommended, but not mandatory.

### AMD (RADV / ACO)

For AMD, RADV is the recommended driver and the one that sees most testing on AMD GPUs.
The recommendation here is to use a driver built from Git.

### NVIDIA

The [Vulkan beta drivers](https://developer.nvidia.com/vulkan-driver) generally contain the latest
driver fixes that we identify while getting games to work.
At least Linux 455.26.01 (2020-10-20) is recommended as it contains fixes for:

> Reduce host memory consumption for descriptor memory when VkDescriptorSetVariableDescriptorCountAllocateInfo is used.

> Fixed a bug in a barrier optimization that allowed some back-to-back copies to run unordered

These fixes should find their way into stable drivers eventually, but if you're having issues, test the latest development drivers,
as that is what we test against.

### Intel

We have not done any testing against Intel iGPUs yet.

------

## Cloning the repo

To clone the repo you should run:
```
git clone --recursive https://github.com/HansKristian-Work/vkd3d-proton
```
in order to pull in all the submodules which are needed for building.

## Building VKD3D-Proton

### Requirements:
- [wine](https://www.winehq.org/) (for `widl`) [for native builds]
  - On Windows this may be substituted for [Strawberry Perl](http://strawberryperl.com/) as it ships `widl` and is easy to find and install -- although this dependency may be eliminated in the future.
- [Meson](http://mesonbuild.com/) build system (at least version 0.49)
- [glslang](https://github.com/KhronosGroup/glslang) compiler
- [Mingw-w64](http://mingw-w64.org/) compiler, headers and tools (at least version 7.0) [for cross-builds for d3d12.dll which are default]

### Building:
#### The simple way
Inside the VKD3D-Proton directory, run:
```
./package-release.sh master /your/target/directory --no-package
```

This will create a folder `vkd3d-master` in `/your/target/directory`, which contains both 32-bit and 64-bit versions of VKD3D-Proton, which can be set up in the same way as the release versions as noted above.

If you want to build natively (ie. for `libvkd3d-proton.so`), pass `--native` to the build script. This option will make it build using your system's compilers.

In order to preserve the build directories for development, pass `--dev-build` to the script. This option implies `--no-package`. After making changes to the source code, you can then do the following to rebuild VKD3D-Proton:
```
# change to build.86 for 32-bit
ninja -C /your/target/directory/build.64 install
```

#### Compiling manually (cross for d3d12.dll, default)
```
# 64-bit build.
meson --cross-file build-win64.txt --buildtype release --prefix /your/vkd3d-proton/directory build.64
ninja -C build.64 install

# 32-bit build
meson --cross-file build-win32.txt --buildtype release --prefix /your/vkd3d-proton/directory build.86
ninja -C build.86 install
```

#### Compiling manually (native)
```
# 64-bit build.
meson --buildtype release --prefix /your/vkd3d-proton/directory build.64
ninja -C build.64 install

# 32-bit build
CC="gcc -m32" CXX="g++ -m32" \
PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig" \
meson --buildtype release --prefix /your/vkd3d-proton/directory build.86
ninja -C build.86 install
```

## Using VKD3D-Proton

The intended way to use VKD3D-Proton is as a native Win32 d3d12.dll.
This serves as a drop-in replacement for D3D12, and can be used in Wine (Proton or vanilla flavors), or on Windows.

VKD3D-Proton does not supply the necessary DXGI component.
VKD3D-Proton can be used with either DXVK's DXGI implementation, or
Wine's DXGI implementation.
VKD3D-Proton implements its own IDXGISwapChain when built as a native d3d12.dll.

### A note on using VKD3D-Proton on Windows

Native Windows use is mostly relevant for developer testing purposes.
Do not expect games running on Windows 7 or 8.1 to magically make use of VKD3D-Proton,
as many games will only even attempt to load d3d12.dll if they are running on Windows 10.

### Native Linux build

A native Linux binary can be built, but it is not intended to be compatible with upstream Wine.
A native option is mostly relevant for development purposes.

## Environment variables

Most of the environment variables used by VKD3D-Proton are for debugging purposes. The
environment variables are not considered a part of API and might be changed or
removed in the future versions of VKD3D-Proton.

Some of debug variables are lists of elements. Elements must be separated by
commas or semicolons.

 - `VKD3D_CONFIG` - a list of options that change the behavior of vkd3d-proton.
    - `vk_debug` - enables Vulkan debug extensions and loads validation layer.
    - `skip_application_workarounds` - Skips all application workarounds.
      For debugging purposes.
    - `dxr` - Enables DXR support if supported by device.
    - `force_static_cbv` - Unsafe speed hack on NVIDIA. May or may not give a significant performance uplift.
 - `VKD3D_DEBUG` - controls the debug level for log messages produced by
   vkd3d-proton. Accepts the following values: none, err, info, fixme, warn, trace.
 - `VKD3D_SHADER_DEBUG` - controls the debug level for log messages produced by
   the shader compilers. See `VKD3D_DEBUG` for accepted values.
 - `VKD3D_LOG_FILE` - If set, redirects `VKD3D_DEBUG` logging output to a file instead.
 - `VKD3D_VULKAN_DEVICE` - a zero-based device index. Use to force the selected
   Vulkan device.
 - `VKD3D_FILTER_DEVICE_NAME` - skips devices that don't include this substring.
 - `VKD3D_DISABLE_EXTENSIONS` - a list of Vulkan extensions that vkd3d-proton should
   not use even if available.
 - `VKD3D_TEST_DEBUG` - enables additional debug messages in tests. Set to 0, 1
   or 2.
 - `VKD3D_TEST_FILTER` - a filter string. Only the tests whose names matches the
   filter string will be run, e.g. `VKD3D_TEST_FILTER=clear_render_target`.
   Useful for debugging or developing new tests.
 - `VKD3D_TEST_PLATFORM` - can be set to "wine", "windows" or "other". The test
   platform controls the behavior of todo(), todo_if(), bug_if() and broken()
   conditions in tests.
 - `VKD3D_TEST_BUG` - set to 0 to disable bug_if() conditions in tests.
 - `VKD3D_PROFILE_PATH` - If profiling is enabled in the build, a profiling block is
   emitted to `${VKD3D_PROFILE_PATH}.${pid}`.

## CPU profiling (development)

Pass `-Denable_profiling=true` to Meson to enable a profiled build. With a profiled build, use `VKD3D_PROFILE_PATH` environment variable.
The profiling dumps out a binary blob which can be analyzed with `programs/vkd3d-profile.py`.
The profile is a trivial system which records number of iterations and total ticks (ns) spent.
It is easy to instrument parts of code you are working on optimizing.

## Advanced shader debugging

These features are only meant to be used by vkd3d-proton developers. For any builtin RenderDoc related functionality
pass `-Denable_renderdoc=true` to Meson.

 - `VKD3D_SHADER_DUMP_PATH` - path where shader bytecode is dumped.
   Bytecode is dumped in format of `$hash.{spv,dxbc,dxil}`.
 - `VKD3D_SHADER_OVERRIDE` - path to where overridden shaders can be found.
   If application is creating a pipeline with `$hash` and `$VKD3D_SHADER_OVERRIDE/$hash.spv` exists,
   that SPIR-V file will be used instead.
 - `VKD3D_AUTO_CAPTURE_SHADER` - If this is set to a shader hash, and the RenderDoc layer is enabled,
 vkd3d-proton will automatically make a capture when a specific shader is encountered.
 - `VKD3D_AUTO_CAPTURE_COUNTS` - A comma-separated list of indices. This can be used to control which queue submissions to capture.
 E.g., use `VKD3D_AUTO_CAPTURE_COUNTS=0,4,10` to capture the 0th (first submission), 4th and 10th submissions which are candidates for capturing.

 If only `VKD3D_AUTO_CAPTURE_COUNTS` is set, any queue submission is considered for capturing.
 If only `VKD3D_AUTO_CAPTURE_SHADER` is set, `VKD3D_AUTO_CAPTURE_COUNTS` is considered to be equal to `"0"`, i.e. a capture is only
 made on first encounter with the target shader.
 If both are set, the capture counter is only incremented and considered when a submission contains the use of the target shader.

### Shader logging

It is possible to log the output of replaced shaders, essentially a custom shader printf. To enable this feature, `VK_KHR_buffer_device_address` must be supported.
First, use `VKD3D_SHADER_DEBUG_RING_SIZE_LOG2=28` for example to set up a 256 MiB ring buffer in host memory.
Since this buffer is allocated in host memory, feel free to make it as large as you want, as it does not consume VRAM.
A worker thread will read the data as it comes in and log it. There is potential here to emit more structured information later.
The main reason this is implemented instead of the validation layer printf system is run-time performance,
and avoids any possible accidental hiding of bugs by introducing validation layers which add locking, etc.
Using `debugPrintEXT` is also possible if that fits better with your debugging scenario.
With this shader replacement scheme, we're able to add shader logging as unintrusive as possible.

Replaced shaders will need to include `debug_channel.h` from `include/shader-debug`.
Use `glslc -I/path/to/vkd3d-proton/include/shader-debug --target-env=vulkan1.1` when compiling replaced shaders.

```
void DEBUG_CHANNEL_INIT(uvec3 ID);
```

is used somewhere in your replaced shader. This should be initialized with `gl_GlobalInvocationID` or similar.
This ID will show up in the log. For each subgroup which calls `DEBUG_CHANNEL_INIT`, an instance counter is generated.
This allows you to correlate several messages which all originate from the same instance counter, which is logged alongside the ID.
An invocation can be uniquely identified with the instance + `DEBUG_CHANNEL_INIT` id.
`DEBUG_CHANNEL_INIT` can be called from non-uniform control flow, as it does not use `barrier()` or similar constructs.
It can also be used in vertex and fragment shaders for this reason.

```
void DEBUG_CHANNEL_MSG();
void DEBUG_CHANNEL_MSG(uint v0);
void DEBUG_CHANNEL_MSG(uint v0, uint v1, ...); // Up to 4 components, can be expanded as needed up to 16.
void DEBUG_CHANNEL_MSG(int v0);
void DEBUG_CHANNEL_MSG(int v0, int v1, ...); // Up to 4 components, ...
void DEBUG_CHANNEL_MSG(float v0);
void DEBUG_CHANNEL_MSG(float v0, float v1, ...); // Up to 4 components, ...
```

These functions log, formatting is `#%x` for uint, `%d` for int and `%f` for float type.

## Descriptor debugging

If `-Denable_descriptor_qa=true` is enabled in build, you can set the `VKD3D_DESCRIPTOR_QA_LOG` env-var to a file.
All descriptor updates and copies are logged so that it's possible to correlate descriptors with
GPU crash dumps. `enable_descriptor_qa` is not enabled by default,
since it adds some flat overhead in an extremely hot code path.

### Debugging descriptor crashes with RADV/ACO dumps (hardcore ultra nightmare mode)

For when you're absolutely desperate, there is a way to debug GPU hangs.
First, install [umr](https://gitlab.freedesktop.org/tomstdenis/umr) and make the binary setsuid.

`ACO_DEBUG=force-waitcnt RADV_DEBUG=hang VKD3D_DESCRIPTOR_QA_LOG=/somewhere/desc.txt %command%`

It is possible to use `RADV_DEBUG=hang,umr` as well, but from within Wine, there are weird things
happening where UMR dumps do not always succeed.
Instead, it is possible to invoke umr manually from an SSH shell when the GPU hangs.

```
#!/bin/bash

mkdir -p "$HOME/umr-dump"

# For Navi, older GPUs might have different rings. See RADV source.
umr -R gfx_0.0.0 > "$HOME/umr-dump/ring.txt" 2>&1
umr -O halt_waves -wa gfx_0.0.0 > "$HOME/umr-dump/halt-waves-1.txt" 2>&1
umr -O bits,halt_waves -wa gfx_0.0.0 > "$HOME/umr-dump/halt-waves-2.txt" 2>&1
```

A folder is placed in `~/radv_dumps*` by RADV, and the UMR script will place wave dumps in `~/umr-dump`.

First, we can study the wave dumps to see where things crash, e.g.:

```
    pgm[6@0x800120e26c00 + 0x584 ] = 0xf0001108		image_load v47, v[4:5], s[48:55] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
    pgm[6@0x800120e26c00 + 0x588 ] = 0x000c2f04	;;
    pgm[6@0x800120e26c00 + 0x58c ] = 0xbf8c3f70		s_waitcnt vmcnt(0)
 *  pgm[6@0x800120e26c00 + 0x590 ] = 0x930118c0		s_mul_i32 s1, 64, s24
    pgm[6@0x800120e26c00 + 0x594 ] = 0xf40c0c09		s_load_dwordx8 s[48:55], s[18:19], s1
    pgm[6@0x800120e26c00 + 0x598 ] = 0x02000000	;;
```

excp: 256 is a memory error (at least on 5700xt).
```
TRAPSTS[50000100]:
	                excp:      256 |         illegal_inst:        0 |           buffer_oob:        0 |           excp_cycle:        0 |
	       excp_wave64hi:        0 |          xnack_error:        1 |              dp_rate:        2 |      excp_group_mask:        0 |
```

We can inspect all VGPRs and all SGPRs, here for the image descriptor.

```
    [  48..  51] = { 0130a000, c0500080, 810dc1df, 93b00204 }
    [  52..  55] = { 00000000, 00400000, 002b0000, 800130c8 }
```

Decode the VA and study `bo_history.log`. There is a script in RADV which lets you query history for a VA.
This lets us verify that the VA in question was freed at some point.
At point of writing, there is no easy way to decode raw descriptor blobs, but when you're desperate enough you can do it by hand :|

In `pipeline.log` we have the full SPIR-V (with OpSource reference to the source DXIL/DXBC)
and disassembly of the crashed pipeline. Here we can study the code to figure out which descriptor was read.

```
    // s7 is the descriptor heap index, s1 is the offset (64 bytes per image descriptor),
    // s[18:19] is the descriptor heap.
    s_mul_i32 s1, 64, s7                                        ; 930107c0
    s_load_dwordx8 s[48:55], s[18:19], s1                       ; f40c0c09 02000000
    s_waitcnt lgkmcnt(0)                                        ; bf8cc07f
    image_load v47, v[4:5], s[48:55] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm ; f0001108 000c2f04
```

```
    [   4..   7] = { 03200020, ffff8000, 0000002b, 00000103 }
```

Which is descriptor index #259. Based on this, we can inspect the descriptor QA log and verify that the application
did indeed do something invalid, which caused the GPU hang.
