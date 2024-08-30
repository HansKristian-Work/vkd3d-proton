static const DWORD execute_indirect_multi_dispatch_root_descriptors_code_dxbc[] =
{
    0x43425844, 0x9de05180, 0xc4ce6696, 0x888b971b, 0x17d7d33f, 0x00000001, 0x000000ac, 0x00000003,
    0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000058, 0x00050050, 0x00000016, 0x0100086a,
    0x0400009e, 0x0011e000, 0x00000000, 0x00000004, 0x0400009b, 0x00000001, 0x00000001, 0x00000001,
    0x0a0000ad, 0x0011e000, 0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00004001, 0x00000001, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE execute_indirect_multi_dispatch_root_descriptors_dxbc = { execute_indirect_multi_dispatch_root_descriptors_code_dxbc, sizeof(execute_indirect_multi_dispatch_root_descriptors_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
