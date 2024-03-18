static const DWORD buffer_feedback_ld_typed_uav_code_dxbc[] =
{
    0x43425844, 0xd7dc177d, 0x5722245f, 0x5af69a70, 0x281d24ee, 0x00000001, 0x0000017c, 0x00000004,
    0x00000030, 0x00000040, 0x00000050, 0x0000016c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
    0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000114, 0x00050050, 0x00000045,
    0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300009d, 0x0011e000, 0x00000000,
    0x0400089c, 0x0011e000, 0x00000001, 0x00004444, 0x0200005f, 0x00020012, 0x02000068, 0x00000002,
    0x0400009b, 0x00000040, 0x00000001, 0x00000001, 0x08000026, 0x0000d000, 0x00100012, 0x00000000,
    0x0002000a, 0x0020800a, 0x00000000, 0x00000000, 0x8b0000e1, 0x80000042, 0x00111103, 0x00100012,
    0x00000000, 0x00100012, 0x00000001, 0x00100006, 0x00000000, 0x0011ee46, 0x00000001, 0x050000ea,
    0x00100042, 0x00000000, 0x0010000a, 0x00000001, 0x09000037, 0x00100022, 0x00000000, 0x0010002a,
    0x00000000, 0x00004001, 0x00000001, 0x00004001, 0x00000000, 0x06000029, 0x00100042, 0x00000000,
    0x0002000a, 0x00004001, 0x00000003, 0x070000a6, 0x0011e032, 0x00000000, 0x0010002a, 0x00000000,
    0x00100046, 0x00000000, 0x0100003e, 0x30494653, 0x00000008, 0x00000900, 0x00000000,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE buffer_feedback_ld_typed_uav_dxbc = { buffer_feedback_ld_typed_uav_code_dxbc, sizeof(buffer_feedback_ld_typed_uav_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
static const BYTE buffer_feedback_ld_typed_uav_code_dxil[] =
{
    0x44, 0x58, 0x42, 0x43, 0xe1, 0xcf, 0xd6, 0xaa, 0xb9, 0x5a, 0xb9, 0x70, 0x75, 0x9c, 0x7d, 0x70, 0x55, 0xe7, 0xa2, 0xbf, 0x01, 0x00, 0x00, 0x00, 0x70, 0x07, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x24, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x98, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69,
    0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x41, 0x7a, 0x6f, 0x31, 0x3f, 0xf9, 0x0c, 0xa9, 0x96, 0xe9, 0x49,
    0xb6, 0xb4, 0x10, 0x81, 0x44, 0x58, 0x49, 0x4c, 0x44, 0x06, 0x00, 0x00, 0x60, 0x00, 0x05, 0x00, 0x91, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x2c, 0x06, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x88, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91,
    0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14,
    0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c,
    0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d, 0x86, 0xf0, 0xff, 0xff,
    0xff, 0xff, 0x03, 0x20, 0x01, 0xd5, 0x06, 0x62, 0xf8, 0xff, 0xff, 0xff, 0xff, 0x01, 0x90, 0x00, 0x49, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x4c, 0x08, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14,
    0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x7c, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0xa0, 0x0c, 0x63, 0x0c, 0x22, 0x37, 0x0d, 0x97, 0x3f, 0x61, 0x0f, 0x21, 0xf9, 0x2b,
    0x21, 0xad, 0xc4, 0xe4, 0x23, 0xb7, 0x8d, 0x8a, 0x31, 0xc6, 0x18, 0x73, 0x04, 0x08, 0x9d, 0x7b, 0x86, 0xcb, 0x9f, 0xb0, 0x87, 0x90, 0xfc, 0x10, 0x68, 0x86, 0x85, 0x40, 0x01, 0x2a, 0x85, 0x19,
    0x69, 0x0c, 0x52, 0x73, 0x04, 0x41, 0x19, 0xd8, 0x18, 0xd4, 0x8e, 0x1a, 0x2e, 0x7f, 0xc2, 0x1e, 0x42, 0xf2, 0xb9, 0x8d, 0x2a, 0x56, 0x62, 0xf2, 0x91, 0xdb, 0x46, 0xc4, 0x18, 0x63, 0x14, 0xe2,
    0x8d, 0x34, 0x08, 0x16, 0x23, 0x8d, 0x33, 0x06, 0x23, 0x59, 0x14, 0x30, 0xd2, 0x18, 0x63, 0x8c, 0x71, 0x88, 0x0e, 0x04, 0x0c, 0x23, 0x10, 0xc3, 0x4c, 0x68, 0x30, 0x0e, 0xec, 0x10, 0x0e, 0xf3,
    0x30, 0x0f, 0x6e, 0x20, 0x0b, 0xb7, 0x20, 0x0a, 0xf5, 0x60, 0x0e, 0xe6, 0x50, 0x0e, 0xf2, 0xc0, 0x07, 0xf6, 0x50, 0x0e, 0xe3, 0x40, 0x0f, 0xef, 0x20, 0x0f, 0x7c, 0x50, 0x0f, 0xee, 0x30, 0x0f,
    0xe9, 0x70, 0x0e, 0xee, 0x50, 0x0e, 0xe4, 0x00, 0x06, 0xe9, 0xe0, 0x0e, 0xf4, 0xc0, 0x06, 0x60, 0x40, 0x07, 0x7e, 0x00, 0x06, 0x7e, 0x80, 0x82, 0x4b, 0xf8, 0x34, 0x69, 0x8a, 0x28, 0x61, 0xf2,
    0x57, 0x78, 0xc3, 0x26, 0x42, 0x1b, 0x86, 0x88, 0x90, 0xa4, 0x8d, 0x2a, 0x0a, 0x22, 0x42, 0xc1, 0x20, 0x7d, 0x04, 0x10, 0x19, 0x12, 0x0a, 0x06, 0xf1, 0x39, 0x02, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30,
    0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a,
    0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07,
    0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60,
    0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x04, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x14, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x34, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xe4,
    0x81, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xc8, 0x23, 0x01, 0x01, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x90, 0x87, 0x02, 0x02, 0x60, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x2c, 0x10, 0x0d, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x1a, 0x25, 0x30, 0x02, 0x50,
    0x10, 0xc5, 0x50, 0x14, 0xa5, 0x50, 0x16, 0x85, 0x50, 0x80, 0x01, 0xc4, 0x46, 0x00, 0xc8, 0x17, 0x28, 0x70, 0x00, 0xed, 0x19, 0x00, 0xea, 0x33, 0x00, 0x94, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x79, 0x18, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0x44, 0x8f, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x24, 0xc6, 0x05, 0xc7, 0x45, 0x86, 0x06, 0x66, 0xc6,
    0x65, 0x86, 0x06, 0x04, 0x65, 0x2c, 0xc7, 0xc6, 0x06, 0x26, 0x0c, 0x67, 0x0c, 0x26, 0x65, 0x43, 0x10, 0x4c, 0x10, 0x86, 0x63, 0x82, 0x30, 0x20, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc9, 0x06,
    0x61, 0x30, 0x28, 0x8c, 0xcd, 0x4d, 0x10, 0x06, 0x65, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x21, 0xa3, 0x08, 0x4c, 0x10, 0x86, 0x65, 0x82, 0xc0, 0x40, 0x13, 0x84, 0x81, 0xd9, 0x20, 0x10, 0xcf, 0x86,
    0x85, 0x58, 0x18, 0x62, 0x18, 0x1a, 0xc7, 0x71, 0xa0, 0x09, 0xc2, 0x26, 0x4d, 0x10, 0x86, 0x66, 0xc3, 0x32, 0x48, 0x0c, 0x41, 0x0c, 0x93, 0xe3, 0x38, 0xc0, 0x06, 0x21, 0xa2, 0x26, 0x08, 0xdd,
    0x34, 0x41, 0x18, 0x9c, 0x0d, 0x08, 0x61, 0x31, 0x04, 0x31, 0x5c, 0xc0, 0x86, 0x00, 0xdb, 0x40, 0x00, 0x55, 0x06, 0x4c, 0x10, 0x04, 0x80, 0x44, 0x5b, 0x58, 0x9a, 0xdb, 0x04, 0xc1, 0x8b, 0x26,
    0x08, 0xc3, 0xb3, 0x61, 0xf0, 0x86, 0x61, 0x03, 0x41, 0x74, 0xd7, 0xb7, 0xa1, 0xd8, 0x38, 0x40, 0x03, 0x83, 0x2a, 0x6c, 0x6c, 0x76, 0x6d, 0x2e, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x53, 0x82, 0xa0,
    0x0a, 0x19, 0x9e, 0x8b, 0x5d, 0x99, 0xdc, 0x5c, 0xda, 0x9b, 0xdb, 0x94, 0x80, 0x68, 0x42, 0x86, 0xe7, 0x62, 0x17, 0xc6, 0x66, 0x57, 0x26, 0x37, 0x25, 0x30, 0xea, 0x90, 0xe1, 0xb9, 0xcc, 0xa1,
    0x85, 0x91, 0x95, 0xc9, 0x35, 0xbd, 0x91, 0x95, 0xb1, 0x4d, 0x09, 0x92, 0x32, 0x64, 0x78, 0x2e, 0x72, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x73, 0x53, 0x82, 0xac, 0x0e, 0x19, 0x9e, 0x4b,
    0x99, 0x1b, 0x9d, 0x5c, 0x1e, 0xd4, 0x5b, 0x9a, 0x1b, 0xdd, 0xdc, 0x94, 0x00, 0x0c, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66,
    0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e,
    0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b,
    0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0,
    0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83,
    0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76,
    0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30,
    0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43,
    0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x0c, 0xc4,
    0x21, 0x07, 0x7c, 0x70, 0x03, 0x7a, 0x28, 0x87, 0x76, 0x80, 0x87, 0x19, 0xd1, 0x43, 0x0e, 0xf8, 0xe0, 0x06, 0xe4, 0x20, 0x0e, 0xe7, 0xe0, 0x06, 0xf6, 0x10, 0x0e, 0xf2, 0xc0, 0x0e, 0xe1, 0x90,
    0x0f, 0xef, 0x50, 0x0f, 0xf4, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x26, 0x40, 0x0d, 0x97, 0xef, 0x3c, 0x7e, 0x40, 0x15, 0x05, 0x11, 0x95, 0x0e, 0x30, 0xf8, 0xc8,
    0x6d, 0x9b, 0x41, 0x35, 0x5c, 0xbe, 0xf3, 0xf8, 0x01, 0x55, 0x14, 0x44, 0xc4, 0x4e, 0x4e, 0x44, 0xf8, 0xc8, 0x6d, 0x1b, 0xc1, 0x36, 0x5c, 0xbe, 0xf3, 0xf8, 0x42, 0x40, 0x15, 0x05, 0x11, 0x95,
    0x0e, 0x30, 0x94, 0x84, 0x01, 0x08, 0x98, 0x8f, 0xdc, 0xb6, 0x0d, 0x80, 0xc1, 0x70, 0xf9, 0xce, 0xe3, 0x0b, 0x07, 0x21, 0x28, 0x9a, 0x20, 0x10, 0x92, 0xf4, 0x51, 0xcb, 0x82, 0x99, 0xc0, 0xf3,
    0x10, 0x83, 0x8f, 0xdc, 0xb6, 0x15, 0x48, 0xc3, 0xe5, 0x3b, 0x8f, 0x2f, 0x44, 0x04, 0x30, 0x11, 0x21, 0xd0, 0x0c, 0x0b, 0x61, 0x01, 0xd2, 0x70, 0xf9, 0xce, 0xe3, 0x4f, 0x47, 0x44, 0x00, 0x83,
    0x38, 0xf8, 0xc8, 0x6d, 0x1b, 0x00, 0xc1, 0x00, 0x48, 0x03, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x34, 0x66, 0x00, 0x4a, 0xae, 0x74, 0x03, 0xca, 0xae, 0x10, 0x03, 0xca, 0x31, 0xa0, 0x0c, 0x4a, 0x31, 0x80, 0x4e, 0x09, 0x14, 0x41, 0x19, 0x00, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x20, 0x6d,
    0xca, 0x90, 0x61, 0xd0, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x48, 0xdc, 0x42, 0x64, 0x5a, 0x34, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x52, 0xc7, 0x10, 0x9a, 0x26, 0x8d, 0x18, 0x18, 0x00, 0x08,
    0x82, 0x01, 0x21, 0x06, 0xcc, 0x36, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x10, 0x18, 0x30, 0x02, 0x37, 0x9a, 0x10, 0x00, 0x15, 0x0c, 0x32, 0x62, 0x80, 0x00, 0x20, 0x08, 0x06, 0x4a, 0x19, 0x38,
    0x46, 0x20, 0x8d, 0x26, 0x04, 0xc0, 0x68, 0x82, 0x40, 0x8c, 0x18, 0x18, 0x00, 0x08, 0x82, 0x41, 0x73, 0x06, 0x50, 0x70, 0xc1, 0x88, 0x4a, 0xa2, 0x1b, 0x31, 0x68, 0x00, 0x10, 0x04, 0x03, 0xea,
    0x0c, 0xa2, 0x26, 0xc0, 0x0a, 0x01, 0xc3, 0x1c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE buffer_feedback_ld_typed_uav_dxil = { buffer_feedback_ld_typed_uav_code_dxil, sizeof(buffer_feedback_ld_typed_uav_code_dxil) };
#undef UNUSED_ARRAY_ATTR
