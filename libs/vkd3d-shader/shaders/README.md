# First Light TrailSort fallback

`first_light_trailsort.hlsl` is an independently written unsigned radix sort
for the specific `TrailSort_CS` bytecode hash used by 007 First Light 1.2.1.0.
The implementation uses shared memory rather than Wave intrinsics. One
1024-thread group sorts `t0[0..N)` using `N = t1[0]`, writing the result to
`u0[0..N)`. For N > 1024, `u0[N..2*N)` is scratch. Dispatch more than one
group is outside this shader's contract.

The original shader was inspected locally to determine its resource and
dispatch contract. Its bytecode is not included. This replacement and its
test were developed with AI assistance.

The workaround requires the game-specific quirk, the known original hash,
a compute pipeline and a Wave64-only device. On Wave32-capable devices the
original shader is compiled unchanged. The replacement is embedded as
DXIL so descriptor bindings and Vulkan capabilities are handled by the
normal compiler, rather than fixed to a particular SPIR-V environment.

Normal builds use the checked-in header. To regenerate it, use Microsoft's
DXC v1.8.2505.1 (Linux package `linux_dxc_2025_07_14.x86_64.tar.gz`):

```sh
python3 libs/vkd3d-shader/shaders/compile_first_light_trailsort.py --dxc /path/to/dxc
python3 libs/vkd3d-shader/shaders/compile_first_light_trailsort.py --dxc /path/to/dxc --check
```

The source-only GPU regression is part of the D3D12 test binary:

```sh
VKD3D_TEST_MATCH=test_first_light_trailsort ./build/tests/d3d12
```

It compares 162 cases against a CPU sort, including empty input, wave and
workgroup boundaries, random data, ascending/descending data, duplicates,
and values on both sides of the unsigned sign bit. It checks untouched
output and guard regions. The test directly exercises the replacement;
selection of the game-specific hashes requires original game shaders,
which are deliberately not redistributed.
