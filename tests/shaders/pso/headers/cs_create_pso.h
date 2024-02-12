static const DWORD cs_create_pso_code_dxbc[] =
{
    0x43425844, 0x1acc3ad0, 0x71c7b057, 0xc72c4306, 0xf432cb57, 0x00000001, 0x00000074, 0x00000003,
    0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000020, 0x00050050, 0x00000008, 0x0100086a,
    0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE cs_create_pso_dxbc = { cs_create_pso_code_dxbc, sizeof(cs_create_pso_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
