static const DWORD dispatch_zero_thread_groups_code_dxbc[] =
{
    0x43425844, 0x3ad946e3, 0x83e33b81, 0x83532aa4, 0x40831f89, 0x00000001, 0x000000b0, 0x00000003,
    0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x0000005c, 0x00050050, 0x00000017, 0x0100086a,
    0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300009d, 0x0011e000, 0x00000000, 0x0400009b,
    0x00000001, 0x00000001, 0x00000001, 0x080000a6, 0x0011e012, 0x00000000, 0x00004001, 0x00000000,
    0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE dispatch_zero_thread_groups_dxbc = { dispatch_zero_thread_groups_code_dxbc, sizeof(dispatch_zero_thread_groups_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
