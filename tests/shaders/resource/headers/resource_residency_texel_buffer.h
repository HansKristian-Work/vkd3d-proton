static const DWORD resource_residency_texel_buffer_code_dxbc[] =
{
    0x43425844, 0xa7b44fd4, 0x3754c17b, 0x39af7f50, 0xb2f301f7, 0x00000001, 0x000000a0, 0x00000003,
    0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x00000008, 0x00000000, 0x00000008, 0x52444853, 0x0000004c, 0x00050050, 0x00000013, 0x0400009b,
    0x00000001, 0x00000001, 0x00000001, 0x0400089c, 0x0011e001, 0x00000000, 0x00004444, 0x0200005f,
    0x00020012, 0x060000a4, 0x0011e0f2, 0x00000000, 0x00020006, 0x00004001, 0x00000064, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE resource_residency_texel_buffer_dxbc = { resource_residency_texel_buffer_code_dxbc, sizeof(resource_residency_texel_buffer_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
