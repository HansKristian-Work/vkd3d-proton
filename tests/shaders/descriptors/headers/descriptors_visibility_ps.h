static const DWORD descriptors_visibility_ps_code_dxbc[] =
{
    0x43425844, 0x7feb7c2e, 0x72c33fa7, 0xd2502eeb, 0xb53ead20, 0x00000001, 0x000002c8, 0x00000003,
    0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
    0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
    0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
    0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000022c, 0x00000050,
    0x0000008b, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300005a, 0x00106000,
    0x00000000, 0x030000a1, 0x00107000, 0x00000000, 0x04001858, 0x00107000, 0x00000001, 0x00005555,
    0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068,
    0x00000001, 0x0b000039, 0x001000f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x00004002,
    0x3f800000, 0x40000000, 0x40400000, 0x40800000, 0x0700003c, 0x00100012, 0x00000000, 0x0010001a,
    0x00000000, 0x0010000a, 0x00000000, 0x0700003c, 0x00100012, 0x00000000, 0x0010002a, 0x00000000,
    0x0010000a, 0x00000000, 0x0700003c, 0x00100012, 0x00000000, 0x0010003a, 0x00000000, 0x0010000a,
    0x00000000, 0x0304001f, 0x0010000a, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
    0x3f800000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e, 0x01000015, 0x890000a5, 0x800002c2,
    0x00199983, 0x00100072, 0x00000000, 0x00004001, 0x00000000, 0x00107246, 0x00000000, 0x0a000027,
    0x00100072, 0x00000000, 0x00100246, 0x00000000, 0x00004002, 0x00000002, 0x00000004, 0x00000008,
    0x00000000, 0x0700003c, 0x00100012, 0x00000000, 0x0010001a, 0x00000000, 0x0010000a, 0x00000000,
    0x0700003c, 0x00100012, 0x00000000, 0x0010002a, 0x00000000, 0x0010000a, 0x00000000, 0x0304001f,
    0x0010000a, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x3f800000, 0x00000000,
    0x00000000, 0x3f800000, 0x0100003e, 0x01000015, 0x0a000038, 0x00100032, 0x00000000, 0x00101046,
    0x00000000, 0x00004002, 0x3d000000, 0x3d000000, 0x00000000, 0x00000000, 0x8b000045, 0x800000c2,
    0x00155543, 0x001020f2, 0x00000000, 0x00100046, 0x00000000, 0x00107e46, 0x00000001, 0x00106000,
    0x00000000, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE descriptors_visibility_ps_dxbc = { descriptors_visibility_ps_code_dxbc, sizeof(descriptors_visibility_ps_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
