static const DWORD ps_integer_blending_no_rt_code_dxbc[] =
{
    0x43425844, 0x499d4ed5, 0xbbe2842c, 0x179313ee, 0xde5cd5d9, 0x00000001, 0x00000064, 0x00000003,
    0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000010, 0x00000050, 0x00000004, 0x0100086a,
    0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE ps_integer_blending_no_rt_dxbc = { ps_integer_blending_no_rt_code_dxbc, sizeof(ps_integer_blending_no_rt_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
