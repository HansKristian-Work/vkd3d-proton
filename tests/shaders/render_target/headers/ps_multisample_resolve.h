static const DWORD ps_multisample_resolve_code_dxbc[] =
{
    0x43425844, 0x68a4590b, 0xc1ec3070, 0x1b957c43, 0x0c080741, 0x00000001, 0x000001c8, 0x00000003,
    0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
    0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
    0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
    0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000012c, 0x00000050,
    0x0000004b, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x04002058, 0x00107000,
    0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2,
    0x00000000, 0x02000068, 0x00000001, 0x06000056, 0x00100012, 0x00000000, 0x0020801a, 0x00000000,
    0x00000000, 0x0700000e, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00100006, 0x00000000,
    0x8900003d, 0x80000102, 0x00155543, 0x001000c2, 0x00000000, 0x00004001, 0x00000000, 0x001074e6,
    0x00000000, 0x07000038, 0x00100032, 0x00000000, 0x00100046, 0x00000000, 0x00100ae6, 0x00000000,
    0x0500001b, 0x00100032, 0x00000000, 0x00100046, 0x00000000, 0x08000036, 0x001000c2, 0x00000000,
    0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8c00002e, 0x80000102, 0x00155543,
    0x001020f2, 0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x0020800a, 0x00000000,
    0x00000000, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE ps_multisample_resolve_dxbc = { ps_multisample_resolve_code_dxbc, sizeof(ps_multisample_resolve_code_dxbc) };
#undef UNUSED_ARRAY_ATTR