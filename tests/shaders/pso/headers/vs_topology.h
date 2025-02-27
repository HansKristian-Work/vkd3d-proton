static const DWORD vs_topology_code_dxbc[] =
{
    0x43425844, 0xaaa241fd, 0xa445aa24, 0x7020d7dc, 0x9dff41f4, 0x00000001, 0x000000b8, 0x00000003,
    0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003, 0x00000000,
    0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x58454853, 0x00000040, 0x00010050, 0x00000010,
    0x0100086a, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x08000036, 0x001020f2, 0x00000000,
    0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE vs_topology_dxbc = { vs_topology_code_dxbc, sizeof(vs_topology_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
static const BYTE vs_topology_code_dxil[] =
{
    0x44, 0x58, 0x42, 0x43, 0x13, 0x7d, 0x3f, 0xd3, 0x97, 0x70, 0x91, 0x15, 0xda, 0xed, 0x50, 0xd2, 0x2a, 0xda, 0x28, 0xd4, 0x01, 0x00, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x1c, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x34, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x50, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00, 0x50, 0x53, 0x56, 0x30, 0x64, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69,
    0x6e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x44, 0x03, 0x03, 0x04, 0x00, 0x00,
    0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x8c, 0x77, 0xe6, 0x95, 0x39, 0x7d, 0xe7, 0x00, 0xcc, 0x8a, 0x27, 0xde, 0xaa, 0x84, 0xd1, 0x44, 0x58, 0x49, 0x4c,
    0x5c, 0x04, 0x00, 0x00, 0x60, 0x00, 0x01, 0x00, 0x17, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x44, 0x04, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde,
    0x21, 0x0c, 0x00, 0x00, 0x0e, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39,
    0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x10, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0x84, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x42, 0x88,
    0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x11, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x21, 0x46, 0x06,
    0x51, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1b, 0x88, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x82, 0x00, 0x00,
    0x89, 0x20, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x32, 0x22, 0x08, 0x09, 0x20, 0x64, 0x85, 0x04, 0x13, 0x22, 0xa4, 0x84, 0x04, 0x13, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x88, 0x8c,
    0x0b, 0x84, 0x84, 0x4c, 0x10, 0x28, 0x23, 0x00, 0x25, 0x00, 0x8a, 0x39, 0x02, 0x30, 0x98, 0x23, 0x40, 0x66, 0x00, 0x8a, 0x01, 0x33, 0x43, 0x45, 0x36, 0x10, 0x90, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30,
    0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a,
    0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07,
    0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60,
    0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x06, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x64, 0x81, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x10, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x9a, 0x12, 0x18, 0x01, 0x28,
    0x88, 0x62, 0x28, 0x83, 0xf2, 0x20, 0x2a, 0x89, 0x32, 0x28, 0x84, 0x11, 0x00, 0xca, 0xb1, 0x04, 0x80, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90,
    0x46, 0x02, 0x13, 0x44, 0x8f, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x24, 0xc6, 0x05, 0xc7, 0x45, 0x86, 0x06, 0xa6, 0xc6, 0x25, 0xa6, 0x06, 0x04, 0xc5, 0x8c, 0xec, 0xa6, 0xac, 0x86, 0x46, 0x6c,
    0x8c, 0x2c, 0x65, 0x43, 0x10, 0x4c, 0x10, 0x06, 0x61, 0x82, 0x30, 0x0c, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc4, 0x06, 0x61, 0x30, 0x28, 0xd8, 0xcd, 0x4d, 0x10, 0x86, 0x62, 0xc3, 0x80, 0x24,
    0xc4, 0x04, 0x21, 0x61, 0x36, 0x04, 0xcb, 0x04, 0x41, 0x00, 0x48, 0xb4, 0x85, 0xa5, 0xb9, 0x71, 0x99, 0xb2, 0xfa, 0x82, 0x7a, 0x9b, 0x4b, 0xa3, 0x4b, 0x7b, 0x73, 0x9b, 0x20, 0x10, 0xc8, 0x04,
    0x81, 0x48, 0x36, 0x04, 0xc4, 0x04, 0x81, 0x50, 0x26, 0x08, 0xc4, 0x32, 0x41, 0x18, 0x8c, 0x09, 0xc2, 0x70, 0x6c, 0x10, 0x2a, 0x6b, 0xc3, 0x42, 0x3c, 0x50, 0x24, 0x4d, 0xc3, 0x44, 0x50, 0xd7,
    0x86, 0x00, 0xdb, 0x30, 0x00, 0x19, 0xb0, 0xa1, 0x68, 0x1c, 0x0d, 0x00, 0xaa, 0xb0, 0xb1, 0xd9, 0xb5, 0xb9, 0xa4, 0x91, 0x95, 0xb9, 0xd1, 0x4d, 0x09, 0x82, 0x2a, 0x64, 0x78, 0x2e, 0x76, 0x65,
    0x72, 0x73, 0x69, 0x6f, 0x6e, 0x53, 0x02, 0xa2, 0x09, 0x19, 0x9e, 0x8b, 0x5d, 0x18, 0x9b, 0x5d, 0x99, 0xdc, 0x94, 0xc0, 0xa8, 0x43, 0x86, 0xe7, 0x32, 0x87, 0x16, 0x46, 0x56, 0x26, 0xd7, 0xf4,
    0x46, 0x56, 0xc6, 0x36, 0x25, 0x48, 0xea, 0x90, 0xe1, 0xb9, 0xd8, 0xa5, 0x95, 0xdd, 0x25, 0x91, 0x4d, 0xd1, 0x85, 0xd1, 0x95, 0x4d, 0x09, 0x96, 0x3a, 0x64, 0x78, 0x2e, 0x65, 0x6e, 0x74, 0x72,
    0x79, 0x50, 0x6f, 0x69, 0x6e, 0x74, 0x73, 0x53, 0x02, 0x0d, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88,
    0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce,
    0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48,
    0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e,
    0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b,
    0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78,
    0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1,
    0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39,
    0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xc8, 0x21, 0x07, 0x7c, 0x70,
    0x03, 0x72, 0x10, 0x87, 0x73, 0x70, 0x03, 0x7b, 0x08, 0x07, 0x79, 0x60, 0x87, 0x70, 0xc8, 0x87, 0x77, 0xa8, 0x07, 0x7a, 0x98, 0x81, 0x3c, 0xe4, 0x80, 0x0f, 0x6e, 0x40, 0x0f, 0xe5, 0xd0, 0x0e,
    0xf0, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x16, 0x50, 0x0d, 0x97, 0xef, 0x3c, 0xbe, 0x34, 0x39, 0x11, 0x81, 0x52, 0xd3, 0x43, 0x4d, 0x7e, 0x71, 0xdb, 0x06, 0x40,
    0x30, 0x00, 0xd2, 0x00, 0x61, 0x20, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x34, 0xa5, 0x40, 0x54, 0x02, 0x45, 0x40, 0x35,
    0x02, 0x30, 0x46, 0x00, 0x82, 0x20, 0x88, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x60, 0x44, 0xc5, 0xf3, 0x1c, 0xc2, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x18, 0x51,
    0xf1, 0x3c, 0x84, 0x30, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x46, 0x54, 0x3c, 0xcf, 0x20, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x11, 0x15, 0xcf, 0x93, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE vs_topology_dxil = { vs_topology_code_dxil, sizeof(vs_topology_code_dxil) };
#undef UNUSED_ARRAY_ATTR
