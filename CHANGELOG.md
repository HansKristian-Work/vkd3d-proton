# Change Log

## 2.7

This release rolls up a massive amount of work since the Steam Deck launch in late February
with mostly features and fixes.

### Heightened driver requirements

Newer extensions are now required.

- `VK_KHR_dynamic_rendering`
- `VK_EXT_extended_dynamic_state`
- `VK_EXT_extended_dynamic_state2` (no optional features required)
- `VK_KHR_maintenance4`

`KHR_dynamic_rendering` in particular requires Mesa 22.0 or NVIDIA 510 series, which should have
propagated to distributions a long time ago at this point.

NOTE: Proton 7.0 stable series will stick to v2.6 to avoid the hard driver requirement bump.
Proton Experimental and any future stable beyond 7.0 will stick to v2.7 and later.

`KHR_dynamic_rendering` fixes many previously unsolvable issues, but it required a rewrite,
and it was deemed impractical to support both legacy and modern paths.

### Improved pipeline cache

v2.6 introduced support for pipeline libraries, but only for games which made correct use of the D3D12 API.
To improve the situation across the board,
vkd3d-proton now implements an internal "magic" disk cache to enable SPIR-V caching for all games.
It is possible to disable the magic cache and let applications manage the ID3D12PipelineLibrary itself if desired.

To further reduce on-disk footprint of the magic cache, we also make use of `VK_EXT_shader_module_identifier`
to reduce the vkd3d-proton cache by >95%, since there is no need to store actual SPIR-V data on-disk.

### Optimizations

Mostly a lot of minor things this release.

- Slightly improve GPU performance for depth render passes.
- Improve GPU performance for certain floating-point images where UAV usage was enabled.
- Improve GPU performance for certain use cases of WriteBufferImmediate().
- Improve GPU performance for certain access patterns of root descriptors.
- Improve GPU performance for back-to-back buffer-image copies.
- Improve GPU performance when allocating large zero-cleared resources and heaps.
- Misc things here and there to reduce overhead.

### New D3D12 features

#### Mesh shaders

`VK_EXT_mesh_shader` is required for this. Directly compatible with D3D12.

#### Advanced ExecuteIndirect

Uses `VK_NV_device_generated_commands`. Supported by both RADV and NVIDIA. Allows Halo Infinite to run.

#### DXR 1.1

Implement some missing features from DXR 1.1:

- AddToStateObject()
- ExecuteIndirect trace rays
- Various complex RTPSO features
- DXIL subobject parsing
- Misc query features

With these fixes in place, e.g. Cyberpunk 2077 DXR works. `VK_KHR_raytracing_maintenance1` is required for some features.

NOTE: `VKD3D_CONFIG=dxr11` is required to enable DXR 1.1 for now.

#### Shared resources

Basic shared resources and fences are now supported when running on Proton. Allows interop with DXVK.
Special thanks to Derek Lesho (@Guy1524) for implementation.

#### SV_Barycentrics

SM 6.1 barycentrics are now exposed through `VK_KHR_fragment_shader_barycentric`.

#### Preliminary HDR support

vkd3d-proton can take advantage of HDR now, assuming the system itself supports it.

### Game fixes and workarounds

- Fix random GPU hangs in Hitman 3.
- Fix crash in Redout 2.
- Fix random GPU hang in F1 2021.
- Fix random flicker in Guardians of the Galaxy.
- Update some API checks required by latest AgilitySDK runtime features. Fix crash in F1 2022.
- Add various workarounds for game bugs in Halo Infinite.
- Add workaround for amdgpu kernel issue for certain games using imported host memory and multiple Vulkan devices.
- Workaround glitched rendering in F1 2020 due to game bug.
- Workaround certain games that violate placed resource API w.r.t. subresource initialization.
  Spiderman Remastered and Lost Judgment are affected. More games will likely surface.

### DXIL support

Countless bug fixes for games released since last release. Too many to enumerate individually.

### Misc

- Improve compatibility with Intel ANV driver.
- Improve correctness of GetFrameLatencyWaitableObject().
- Add BLOB PIX decoding.
- Improve stability when minimizing and alt-tabbing in and out of fullscreen in some games.
- Preparation for MIT re-license is underway.

### Stronger debugging facilities

- For developers and power users, a breadcrumbs functionality is added to greatly aid GPU hang debugging.
  Requires either `VK_AMD_buffer_marker` or `VK_NV_device_diagnostic_checkpoints`.
- When capturing with RenderDoc, cached host memory is enabled by default to speed up capture and improve stability.
- Improve shader replacement system ease-of-use.

## 2.6

It has been a long while since 2.5, and this release rolls up a lot of fixes, features and optimizations.

### Fixes

- Fix black screen rendering bug in Horizon Zero Dawn after latest game updates.
- Fix crashes on startup in Final Fantasy VII: Remake and Warframe.
- Fix crashes in Guardians of the Galaxy when interacting with certain game objects.
- Fix hang on game shutdown in Elden Ring.
- Fix broken geometry rendering in Age of Empires: IV.

### Optimization

- Improve generated shader code for vectorized load-store operations in DXIL.
- Greatly reduce CPU overhead for descriptor copy operations,
  which is a key contributor to CPU overhead in D3D12.

### Features

#### Pipeline library rewrite

Support D3D12 pipeline libraries better where we can now also cache
generated SPIR-V from DXBC/DXIL.
Massively reduces subsequent load times in Monster Hunter: Rise,
and helps other titles like Guardian of the Galaxy and Elden Ring.
Also lays the groundwork for internal driver caches down the line for games which do not use this API.
Also, deduplicates binary blobs for reduced disk size requirements.

#### Shader models

Shader model 6.6 is now fully implemented. This includes support for:
- ResourceDescriptorHeap[] direct access
- 64-bit atomics
- IsHelperLane()
- Compute shader derivatives
- WaveSize attribute
- Packed math intrinsics

#### Minor features

- Handle API feature MinResourceLODClamp correctly if `VK_EXT_image_view_min_lod` is supported.
- Expose CastFullyTypedFormat feature.
- Expose some advanced shader features on Intel related to UAV formats (`VK_KHR_format_feature_flags2`).
- Support COLOR -> STENCIL copies.

### Workarounds

- Workaround DEATHLOOP not emitting synchronization commands correctly. Fixes menu flicker on RADV.
- Workaround quirky API usage in Elden Ring. Removes many kinds of stutter and chug when traversing the scenery.
- Workaround certain environments failing to create Vulkan device if some `VK_NVX_*` extensions are enabled.
- Workaround glitched foliage rendering in Horizon Zero Dawn after latest game updates.
- Workaround some questionable UE4 shaders causing glitched rendering on RADV.

### Note on future Vulkan driver requirements

2.6 is expected to be the last vkd3d-proton release before we require some newer Vulkan extensions.
`VK_KHR_dynamic_rendering` and `VK_EXT_extended_dynamic_state`
(and likely `dynamic_state_2` as well) will be required.

`VK_KHR_dynamic_rendering` in particular requires up-to-date drivers and the legacy render pass path
will be abandoned in favor of it. Supporting both paths at the same time is not practical.
Moving to `VK_KHR_dynamic_rendering` allows us to fix some critical flaws with the legacy API
which caused potential shader compilation stutters and extra CPU overhead.

## 2.5

This is a release with a little bit of everything!

### Features

#### DXR progress

DXR has seen significant work in the background.

- DXR 1.1 is now experimentally exposed. It can be enabled with `VKD3D_CONFIG=dxr11`.
  Note that DXR 1.1 cannot be fully implemented in `VK_KHR_ray_tracing`'s current form, in particular
  DispatchRays() indirect is not compatible yet,
  although we have not observed a game which requires this API feature.
- DXR 1.1 inline raytracing support is fully implemented.
- DXR 1.0 support is more or less feature complete.
  Some weird edge cases remain, but will likely not be implemented unless required by a game.
  `VKD3D_CONFIG=dxr` will eventually be dropped when it matures.

Some new DXR games are starting to come alive, especially with DXR 1.1 enabled,
but there are significant bugs as well that we currently cannot easily debug.
Some experimental results on NVIDIA:

- **Control** - already worked
- **DEATHLOOP** - appears to work correctly
- **Cyberpunk 2077** - DXR can be enabled, but GPU timeouts
- **World of Warcraft** - according to a user, it works, but we have not confirmed ourselves
- **Metro Exodus: Enhanced Edition** -
    gets ingame and appears to work? Not sure if it looks correct.
    Heavy CPU stutter for some reason ...
- **Metro Exodus** (original release) - GPU timeouts when enabling DXR
- **Resident Evil: Village** - Appears to work, but the visual difference is subtle.

It's worth experimenting with these and others.
DXR is incredibly complicated, so expect bugs.
From here, DXR support is mostly a case of stamping out issues one by one.

#### NVIDIA DLSS

NVIDIA contributed integration APIs in vkd3d-proton which enables DLSS support in D3D12 titles in Proton.
See Proton documentation for how to enable NvAPI support.

#### Shader models

A fair bit of work went into DXIL translation support to catch up with native drivers.

- Shader model 6.5 is exposed.
  Shader model 6.6 should be straight forward once that becomes relevant.
- Shader model 6.4 implementation takes advantage of `VK_KHR_shader_integer_dot_product` when supported.
- Proper fallback for FP16 math on GPUs which do not expose native FP16 support (Polaris, Pascal).
  Notably fixes AMD FSR shaders in Resident Evil: Village (and others).
- Shader model 6.1 SV_Barycentric support implemented (NVIDIA only for now).
- Support shader model 6.2 FP32 denorm control.

### Performance

Resizable BAR can improve GPU performance about 10-15% in the best case, depends a lot on the game.
Horizon Zero Dawn and Death Stranding in particular improve massively with this change.

By default, vkd3d-proton will now take advantage of PCI-e BAR memory types through heuristics
as D3D12 does not expose direct support for resizable BAR, and native D3D12 drivers are known to use heuristics as well.
Without resizable BAR enabled in BIOS/vBIOS, we only get 256 MiB which can help performance,
but many games will improve performance even more
when we are allowed to use more than that.
There is an upper limit for how much VRAM is dedicated to this purpose.
We also added `VKD3D_CONFIG=no_upload_hvv` to disable all uses of PCI-e BAR memory.

Other performance improvements:

- Avoid redundant descriptor update work in certain scenarios (NVIDIA contribution).
- Minor tweaks here and there to reduce CPU overhead.

### Fixes and workarounds

- Fix behavior for swap chain presentation latency HANDLE. Fixes spurious deadlocks in some cases.
- Fix many issues related to depth-stencil handling, which fixed various issues in DEATHLOOP, F1 2021, WRC 10.
- Fix DIRT 5 rendering issues and crashes. Should be fully playable now.
- Fix some Diablo II Resurrected rendering issues.
- Workaround shader bugs in Psychonauts 2.
- Workaround some Unreal Engine 4 shader bugs which multiple titles trigger.
- Fix some stability issues when VRAM is exhausted on NVIDIA.
- Fix CPU crash in boot-up sequence of Far Cry 6 (game is still kinda buggy though, but gets in-game).
- Fix various bugs with host visible images. Fixes DEATHLOOP.
- Fix various DXIL conversion bugs.
- Add Invariant geometry workarounds for specific games which require it.
- Fix how d3d12.dll exports symbols to be more in line with MSVC.
- Fix some edge cases in bitfield instructions.
- Work around extreme CPU memory bloat on the specific NVIDIA driver versions which had this bug.
- Fix regression in Evil Genius 2: World Domination.
- Fix crashes in Hitman 3.
- Fix terrain rendering in Anno 1800.
- Various correctness and crash fixes.

## 2.4

This is a release which focuses on performance and bug-fixes.

### Performance

- Improve swapchain latency and frame pacing by up to one frame.
- Optimize lookup of format info.
- Avoid potential pipeline compilation stutter in certain scenarios.
- Rewrite how we handle image layouts for color and depth-stencil targets.
  Allows us to remove a lot of dumb
  barriers giving significant GPU-bound performance improvements.
  ~15%-20% GPU bound uplift in Horizon Zero Dawn,
  ~10% in Death Stranding,
  and 5%-10% improvements in many other titles.

### Features

- Enable support for sparse 3D textures (tiled resources tier 3).

### Bug fixes and workarounds

- Various bug fixes in DXIL.
- Fix weird bug where sun would pop through walls in RE: Village.
- Workaround game bug in Cyberpunk 2077 where certain locales would render a black screen.
- Fix various bugs (in benchmark and in vkd3d-proton) allowing GravityMark to run.
- Improve robustness against certain app bugs related to NULL descriptors.
- Fix bug with constant FP64 vector handling in DXBC.
- Fix bug where Cyberpunk 2077 inventory screen could spuriously hang GPU on RADV.
- Add workaround for Necromunda: Hired Gun where character models would render random garbage on RADV.
- Fix bug in Necromunda: Hired Gun causing random screen flicker.
- Fix windowed mode tracking when leaving fullscreen. Fix Alt-Tab handling in Horizon Zero Dawn.
- Temporary workaround for SRV ResourceMinLODClamp. Fix black ground rendering in DIRT 5.
  The overbright HDR rendering in DIRT 5 sadly persists however :(
- Implement fallback maximum swapchain latency correctly.

### Development features

Various features which are useful for developers were added to aid debugging.

- Descriptor QA can instrument shaders in runtime for GPU-assisted validation.
  Performance is good enough (> 40 FPS) that games are actually playable in this mode.
  See README for details.
- Allow forcing off CONCURRENT queue, and using EXCLUSIVE queue.
  Not valid, but can be useful as a speed hack on Polaris when `single_queue` is not an option
  and for testing driver behavior differences.

## 2.3.1

This is a minor bugfix release to address some issues solved shortly after the last release.

### Fixes

- Improved support for older Wine and Vulkan Loader versions.
- Fix blocky shadows in Horizon Zero Dawn.
- Fix the install script failing on Wine installs not built with upstream vkd3d.
- Fix minor dxil translation issues.

## 2.3

This release adds support for more D3D12 features and greatly improves GPU bound performance
in many scenarios.

### Features

#### Early DXR 1.0 support

`VK_KHR_raytracing` is used to enable cross-vendor ray-tracing support.
The implementation is WIP, but it is good enough to run some real content.

As of writing, only the NVIDIA driver works correctly.
It is expected AMD RDNA2 GPUs will work when working drivers are available
(amdgpu-pro 21.10 is known to not work).

Games which are expected to work include:
- Control (appears to be fully working)
- Ghostrunner (seems to work, not exhaustively tested)

To enable DXR support, `VKD3D_CONFIG=dxr %command%` should be used when launching game.
Certain games may be unstable if DXR is enabled by default.

#### Conservative rasterization

Full support (tier 3) for conservative rasterization was added.

#### Variable rate shading

Full support (tier 2) for variable rate shading was added.

#### Command list bundles

Allows Kingdom Hearts remaster to get past the errors, unsure if game fully works yet.

#### Write Watch and APITrace

Support for `D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH` has been added.
This means [APITraces](https://github.com/Joshua-Ashton/apitrace/releases) of titles can now be captured.

### Performance

- Improve GPU bound performance in RE2 by up to 20% on NVIDIA.
- Enable async compute queues. Greatly improves GPU performance and frame pacing in many titles.
  Horizon Zero Dawn and Death Stranding see exceptional gains with this fix,
  due to how the engines work. GPU utilization should now reach ~100%.
  For best results, AMD Navi+ GPUs are recommended, but Polaris and earlier still
  see great results. It is possible to disable this path, if for whatever reason
  multiple queues are causing issues. See README.
- Optimize bindless constant buffer GPU-bound performance on NVIDIA if certain API code paths are used.
- Optimize sparse binding CPU overhead.
- `TRACE` logging calls are disabled by default on release builds.

### Fixes and workarounds

- Fix various DXIL bugs.
- Be more robust against broken pipeline creation API calls.
  Avoids driver crashes in Forza Horizon 4.
- Workaround some buggy shaders in F1 2020.
- Fix bugs if depth bounds test is used in certain ways.
- Fix a read out-of-bounds in `UpdateTileMappings`.
- Fix `SV_ClipDistance` and `SV_CullDistance` in Hull Shaders.

## 2.2

This release is mostly a maintenance release which fixes bugs and regressions.
It also unblocks significant future feature development.

### Workaround removals

- Replace old `force_bindless_texel_buffer` workaround with
  a more correct and performant implementation.
  Death Stranding and Cyberpunk 2077 (and probably other games as well) do the right thing by default without the hack now.
- Remove old workaround `disable_query_optimization` for occlusion queries which was enabled for AC: Valhalla,
  and is now replaced by a correct and efficient implementation.

#### Cyberpunk 2077 status
From recent testing on our end, it is unknown at this time if `VK_VALVE_mutable_descriptor_type` is still required for
Cyberpunk 2077. Manual testing hasn't been able to trigger a GPU hang.
The memory allocation rewrite in 2.2 can plausibly work around some of the bugs that `VK_VALVE_mutable_descriptor_type` fixed by accident.
The bugs in question could also have been fixed since release day, but we cannot prove this since the bug is completely random in nature.

### Regression fixes

- Fix regression in Horizon Zero Dawn for screen space reflections on water surfaces.

### Stability fixes

- Greatly improve stability on Polaris or older cards for certain titles.
  Crashes which used to happen in Horizon Zero Dawn and Death Stranding seem to have disappeared
  after the memory allocation rewrite.
  GPU memory usage should decrease on these cards as well.
- DIRT 5 can get in-game now due to DXIL fixes, but is not yet playable.

### New features

- Add support for Variable Rate Shading tier 1.

### Future development

DXR is not yet supported, but has seen a fair bit of background work.

- Basic DXR pipelines can be created successfully.
- Memory allocation rewrite in 2.2 unblocks further DXR development.

## 2.1

This release fixes various bugs (mostly workarounds) and improves GPU-bound performance.

New games added to "expected to work" list:
 - The Division (was working already in 2.0, but missing from list)
 - AC: Valhalla (*)

(*): Game requires full D3D12 sparse texture support to work.
Currently only works on NVIDIA drivers.
RADV status remains unknown until support for this feature lands in Mesa.

New games added to "kinda works, but expect a lot of jank" list:
 - Cyberpunk 2077 (**)

(**): Currently only runs correctly on AMD hardware with RADV and `VK_VALVE_mutable_descriptor_type`.
As of game version 1.03, this requires the latest Mesa Git build.
The game has some fatal bugs where it relies on undefined behavior with descriptor management
which this extension works around by accident.
The game will start and run on NVIDIA, but just like what happens without the extension on AMD,
the GPU will randomly hang, making the game effectively unplayable.
A game update to fix this bug would likely make the game playable on NVIDIA as well.
Game version 1.04 changed some behavior, and support for this game will likely fluctuate over time as future patches come in.

Bug fixes and workarounds:
 - Fix various implementation bugs which caused AC: Valhalla to not work.
 - Work around game bug in Death Stranding where accessing map could cause corrupt rendering.
   (Several games appear to have the same kind of application bug.)
 - Fix corrupt textures in Horizon Zero Dawn benchmark.
 - Fix SM 6.0 wave-op detection for Horizon Zero Dawn and DIRT 5.
 - Work around GPU hangs in certain situations where games do not use D3D12 correctly,
   but native D3D12 drivers just render wrong results rather than hang the system.
 - Fix invalid SPIR-V generated by FP64 code.
 - Fix crash with minimized windows in certain cases.

Performance:
 - ~15% GPU-bound uplift in Ghostrunner. Might help UE4 titles in general.
 - Slightly improve GPU bound performance when fully GPU bound on both AMD and NVIDIA.
 - Slightly improve GPU bound performance on RADV in various titles.
 - Reduce multi-threaded CPU overhead for certain D3D12 API usage patterns.
 - Add support for `VK_VALVE_mutable_descriptor_type` which
   improves CPU overhead, memory bloat, and avoids potential memory management thrashing on RADV.
   Also avoids GPU hangs in certain situations where games misuse the D3D12 API.

Misc:
 - Implement `DXGI_PRESENT_TEST`.
 - Fix log spam when `DXGI_PRESENT_ALLOW_TEARING` is used.

## 2.0

This initial release supports D3D12 Feature Level 12.0 and Shader Model 6.0 (DXIL).

Games expected to work include:

 - Control
 - Death Stranding
 - Devil May Cry 5
 - Ghostrunner
 - Horizon Zero Dawn
 - Metro Exodus
 - Monster Hunter World
 - Resident Evil 2 / 3

Please refer to the README for supported driver versions.

