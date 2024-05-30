static const DWORD ps_resolve_setup_stencil_code_dxbc[] =
{
    0x43425844, 0x2fc829d6, 0x337f408a, 0x9ea69217, 0x4ae9bda5, 0x00000001, 0x000000b4, 0x00000003,
    0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000001, 0xffffffff,
    0x00000e01, 0x435f5653, 0x7265766f, 0x00656761, 0x58454853, 0x0000003c, 0x00000050, 0x0000000f,
    0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x02000065, 0x0000f000, 0x05000036,
    0x0000f001, 0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE ps_resolve_setup_stencil_dxbc = { ps_resolve_setup_stencil_code_dxbc, sizeof(ps_resolve_setup_stencil_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
