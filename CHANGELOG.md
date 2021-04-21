# Change Log

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

