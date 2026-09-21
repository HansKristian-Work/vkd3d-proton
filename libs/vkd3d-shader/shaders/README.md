# First Light TrailSort fallback

One 1024-thread group sorts unsigned `t0[0..N)`, where `N = t1[0]`, into
`u0[0..N)`. For N > 1024, `u0[N..2*N)` is scratch. The implementation uses
shared memory and has no fixed wave-size requirement. Dispatching more
than one group is outside its contract.

The fallback is limited to the known First Light 1.2.1.0 shader hash and
Wave64-only devices. DXIL is embedded so the normal compiler handles the
current root signature and device capabilities.

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

It compares 162 cases against a CPU sort and checks untouched output and
guard regions. It tests the replacement directly; the original game
bytecode used to check automatic workaround selection is not included.
