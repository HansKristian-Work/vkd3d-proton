static const DWORD renderpass_rendering_code_dxbc[] =
{
    0x43425844, 0x19e95b1b, 0x8933e8f2, 0xf2efe245, 0xbd578427, 0x00000001, 0x00000374, 0x00000003,
    0x0000002c, 0x0000003c, 0x000000e4, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x000000a0, 0x00000005, 0x00000008, 0x00000080, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
    0x0000000f, 0x00000080, 0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x00000080,
    0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000000f, 0x0000008a, 0x00000000, 0x00000000,
    0x00000001, 0xffffffff, 0x00000e01, 0x00000096, 0x00000000, 0x00000000, 0x00000003, 0xffffffff,
    0x00000e01, 0x545f5653, 0x65677261, 0x56530074, 0x766f435f, 0x67617265, 0x56530065, 0x7065445f,
    0xab006874, 0x58454853, 0x00000288, 0x00000050, 0x000000a2, 0x0100086a, 0x04000059, 0x00208e46,
    0x00000000, 0x00000002, 0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000001,
    0x03000065, 0x001020f2, 0x00000002, 0x02000065, 0x0000f000, 0x02000065, 0x0000c001, 0x02000068,
    0x00000004, 0x05000036, 0x0000c001, 0x0020800a, 0x00000000, 0x00000000, 0x1000008a, 0x001000f2,
    0x00000000, 0x00004002, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00004002, 0x00000008,
    0x00000010, 0x00000008, 0x00000010, 0x00208a56, 0x00000000, 0x00000000, 0x05000056, 0x00100062,
    0x00000001, 0x00100106, 0x00000000, 0x05000056, 0x00100062, 0x00000000, 0x00100ba6, 0x00000000,
    0x0b000001, 0x00100072, 0x00000002, 0x00208796, 0x00000000, 0x00000000, 0x00004002, 0x000000ff,
    0x000000ff, 0x000000ff, 0x00000000, 0x05000056, 0x00100012, 0x00000001, 0x0010000a, 0x00000002,
    0x0b000055, 0x00100072, 0x00000003, 0x00208796, 0x00000000, 0x00000000, 0x00004002, 0x00000018,
    0x00000018, 0x00000018, 0x00000000, 0x05000056, 0x00100082, 0x00000001, 0x0010000a, 0x00000003,
    0x0a000038, 0x001020f2, 0x00000000, 0x00100e46, 0x00000001, 0x00004002, 0x3b808081, 0x3b808081,
    0x3b808081, 0x3b808081, 0x05000056, 0x00100012, 0x00000000, 0x0010001a, 0x00000002, 0x05000056,
    0x00100012, 0x00000001, 0x0010002a, 0x00000002, 0x05000056, 0x00100082, 0x00000000, 0x0010001a,
    0x00000003, 0x05000056, 0x00100082, 0x00000001, 0x0010002a, 0x00000003, 0x0a000038, 0x001020f2,
    0x00000001, 0x00100e46, 0x00000000, 0x00004002, 0x3b808081, 0x3b808081, 0x3b808081, 0x3b808081,
    0x1000008a, 0x00100032, 0x00000000, 0x00004002, 0x00000008, 0x00000008, 0x00000000, 0x00000000,
    0x00004002, 0x00000008, 0x00000010, 0x00000000, 0x00000000, 0x00208ff6, 0x00000000, 0x00000000,
    0x05000056, 0x00100062, 0x00000001, 0x00100106, 0x00000000, 0x0a000038, 0x001020f2, 0x00000002,
    0x00100e46, 0x00000001, 0x00004002, 0x3b808081, 0x3b808081, 0x3b808081, 0x3b808081, 0x05000036,
    0x0000f001, 0x0020800a, 0x00000000, 0x00000001, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE renderpass_rendering_dxbc = { renderpass_rendering_code_dxbc, sizeof(renderpass_rendering_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
