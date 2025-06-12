static const DWORD ps_early_depth_stencil_code_dxbc[] =
{
    0x43425844, 0xd8c9f845, 0xadb9dbe2, 0x4e8aea86, 0x80f0b053, 0x00000001, 0x0000009c, 0x00000003,
    0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000048, 0x00000050, 0x00000012, 0x0100286a,
    0x0400189c, 0x0011e000, 0x00000000, 0x00003333, 0x0a0000ad, 0x0011e000, 0x00000000, 0x00004002,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00004001, 0x00000001, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE ps_early_depth_stencil_dxbc = { ps_early_depth_stencil_code_dxbc, sizeof(ps_early_depth_stencil_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
