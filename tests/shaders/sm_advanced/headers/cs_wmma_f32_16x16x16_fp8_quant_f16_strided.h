static const BYTE cs_wmma_f32_16x16x16_fp8_quant_f16_strided_code_dxil[] =
{
    0x44, 0x58, 0x42, 0x43, 0xb2, 0x07, 0xa8, 0x04, 0x4e, 0x70, 0x4e, 0x02, 0x9c, 0xa8, 0xd3, 0x88, 0x47, 0xbe, 0x88, 0xf2, 0x01, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x24, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x98, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0xde, 0x0a, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69,
    0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xb7, 0x66, 0x33, 0x22, 0x6d, 0xa8, 0xf9, 0x2e, 0x5e, 0xe0, 0x1c,
    0x41, 0x59, 0x94, 0x8e, 0x44, 0x58, 0x49, 0x4c, 0xd4, 0x0f, 0x00, 0x00, 0x66, 0x00, 0x05, 0x00, 0xf5, 0x03, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x06, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0xbc, 0x0f, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0xec, 0x03, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91,
    0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14,
    0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c,
    0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xaa, 0x0d, 0x84, 0xf0, 0xff, 0xff,
    0xff, 0xff, 0x03, 0x20, 0x6d, 0x30, 0x86, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x09, 0xa0, 0x00, 0x49, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x4c, 0x08, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14,
    0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x64, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0xe0, 0xa6, 0xe1, 0xf2, 0x27, 0xec, 0x21, 0x24, 0x7f, 0x25, 0xa4, 0x95, 0x98, 0x7c,
    0xe4, 0xb6, 0x51, 0x31, 0xc6, 0x18, 0x63, 0x8e, 0x00, 0xa1, 0x72, 0xcf, 0x70, 0xf9, 0x13, 0xf6, 0x10, 0x92, 0x1f, 0x02, 0xcd, 0xb0, 0x10, 0x28, 0x30, 0xe5, 0x20, 0xe3, 0x8c, 0x51, 0x06, 0xa1,
    0x82, 0x8c, 0x71, 0xc6, 0x18, 0x63, 0x90, 0xba, 0x6d, 0xb8, 0xfc, 0x09, 0x7b, 0x08, 0xc9, 0x5f, 0x09, 0xc9, 0xa1, 0x22, 0x81, 0x48, 0x23, 0xe7, 0x21, 0xa2, 0x09, 0x21, 0x24, 0x24, 0x8c, 0x51,
    0x88, 0x33, 0x0e, 0xa3, 0x76, 0xd0, 0x70, 0xf9, 0x13, 0xf6, 0x10, 0x92, 0xbf, 0x12, 0xd2, 0x86, 0x34, 0x03, 0x22, 0xc6, 0x18, 0x65, 0x8e, 0x20, 0x28, 0xc5, 0x19, 0x6f, 0x40, 0x8a, 0x03, 0x01,
    0x87, 0x49, 0x53, 0x44, 0x09, 0x93, 0xbf, 0x61, 0x13, 0xa1, 0x0d, 0x43, 0x44, 0x48, 0xd2, 0x46, 0x15, 0x05, 0x11, 0xa1, 0x60, 0x10, 0x3d, 0x4d, 0x9a, 0x22, 0x4a, 0x98, 0xfc, 0x15, 0xde, 0xb0,
    0x89, 0xd0, 0x86, 0x21, 0x22, 0x24, 0x69, 0xa3, 0x8a, 0x82, 0x88, 0x50, 0x30, 0xc8, 0xce, 0x11, 0x80, 0x02, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79,
    0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0,
    0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73,
    0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07,
    0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x08, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x14, 0x20, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x34, 0x40, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xe4, 0x89, 0x80, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
    0x0b, 0x04, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x1a, 0x25, 0x50, 0x0c, 0x05, 0x31, 0x02, 0x50, 0x16,
    0xc5, 0xbb, 0x8a, 0xff, 0xff, 0x50, 0x08, 0x05, 0x44, 0x70, 0x04, 0x80, 0x70, 0x81, 0xd0, 0x9d, 0x01, 0xa0, 0x3a, 0x03, 0x00, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x8e, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x24, 0xc6, 0x05, 0xc7, 0x45, 0x86, 0x06, 0xe6, 0xc6, 0xe5, 0x06, 0x04, 0x85, 0x26, 0xc6, 0xc6, 0x2c,
    0x4c, 0xcc, 0x46, 0xac, 0x26, 0x65, 0x43, 0x10, 0x4c, 0x10, 0x86, 0x62, 0x82, 0x30, 0x18, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc7, 0x06, 0x61, 0x30, 0x28, 0x8c, 0xcd, 0x6d, 0x18, 0x10, 0x82,
    0x98, 0x20, 0x0c, 0xc8, 0x04, 0xa1, 0x82, 0x08, 0x4c, 0x10, 0x86, 0x64, 0x43, 0xa2, 0x2c, 0x8c, 0xa2, 0x0c, 0x8d, 0x02, 0x6c, 0x08, 0x9c, 0x09, 0xc2, 0xf5, 0x4c, 0x10, 0xa0, 0x66, 0xc3, 0xa2,
    0x40, 0x8c, 0xa2, 0x0c, 0x4d, 0x14, 0x45, 0xc0, 0x04, 0x61, 0x50, 0x36, 0x2c, 0x03, 0xc4, 0x4c, 0xca, 0xd0, 0x44, 0x51, 0x04, 0x6c, 0x10, 0x24, 0x6a, 0x03, 0xf1, 0x54, 0x00, 0x30, 0x41, 0x10,
    0x00, 0x12, 0x6d, 0x61, 0x69, 0x6e, 0x13, 0x04, 0xcc, 0x99, 0x20, 0x0c, 0xcb, 0x04, 0x61, 0x60, 0x36, 0x0c, 0xdb, 0x30, 0x6c, 0x08, 0xb6, 0x0d, 0x86, 0x92, 0x69, 0x5c, 0xd3, 0x6d, 0x28, 0x2e,
    0x0c, 0xb0, 0xbc, 0x2a, 0x6c, 0x6c, 0x76, 0x6d, 0x2e, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x53, 0x82, 0xa0, 0x0a, 0x19, 0x9e, 0x8b, 0x5d, 0x99, 0xdc, 0x5c, 0xda, 0x9b, 0xdb, 0x94, 0x80, 0x68, 0x42,
    0x86, 0xe7, 0x62, 0x17, 0xc6, 0x66, 0x57, 0x26, 0x37, 0x25, 0x30, 0xea, 0x90, 0xe1, 0xb9, 0xcc, 0xa1, 0x85, 0x91, 0x95, 0xc9, 0x35, 0xbd, 0x91, 0x95, 0xb1, 0x4d, 0x09, 0x90, 0x32, 0x64, 0x78,
    0x2e, 0x72, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x73, 0x53, 0x82, 0xaa, 0x0e, 0x19, 0x9e, 0x4b, 0x99, 0x1b, 0x9d, 0x5c, 0x1e, 0xd4, 0x5b, 0x9a, 0x1b, 0xdd, 0xdc, 0x94, 0xc0, 0x03, 0x00,
    0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73,
    0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b,
    0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20,
    0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61,
    0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87,
    0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98,
    0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61,
    0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b,
    0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x0c, 0xc4, 0x21, 0x07, 0x7c, 0x70, 0x03, 0x7a, 0x28, 0x87, 0x76, 0x80, 0x87, 0x19, 0xd1, 0x43, 0x0e, 0xf8,
    0xe0, 0x06, 0xe4, 0x20, 0x0e, 0xe7, 0xe0, 0x06, 0xf6, 0x10, 0x0e, 0xf2, 0xc0, 0x0e, 0xe1, 0x90, 0x0f, 0xef, 0x50, 0x0f, 0xf4, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x36, 0x40, 0x0d, 0x97, 0xef, 0x3c, 0x3e, 0xd0, 0x34, 0xce, 0x04, 0x4c, 0x44, 0x08, 0x34, 0xc3, 0x42, 0x98, 0xc0, 0x37, 0x5c, 0xbe, 0xf3, 0xf8, 0xc0, 0xe4, 0x30, 0x88, 0xc0, 0x39, 0xcc, 0x03,
    0x44, 0x84, 0x77, 0x09, 0x07, 0xd0, 0x18, 0x84, 0x8f, 0xdc, 0xb6, 0x11, 0x74, 0xc3, 0xe5, 0x3b, 0x8f, 0x2f, 0x44, 0x04, 0x30, 0x11, 0x21, 0xd0, 0x0c, 0x0b, 0xf1, 0x45, 0x0e, 0xb3, 0x21, 0xcd,
    0x80, 0x34, 0x86, 0x05, 0x5c, 0xc3, 0xe5, 0x3b, 0x8f, 0x1f, 0x01, 0xd6, 0x46, 0x15, 0x05, 0x11, 0x95, 0x0e, 0x30, 0xf8, 0xc8, 0x6d, 0x1b, 0x00, 0xc1, 0x00, 0x48, 0x03, 0x61, 0x20, 0x00, 0x00,
    0xa6, 0x02, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x55, 0x00, 0x00, 0x00, 0x34, 0x66, 0x00, 0x0a, 0x76, 0xa0, 0x3c, 0x03, 0xca, 0x52, 0xa0, 0x64, 0x07, 0x4a, 0x1a, 0x10,
    0x10, 0x50, 0xa1, 0xa4, 0x01, 0x01, 0x41, 0x15, 0x4a, 0x9a, 0x10, 0x10, 0x54, 0xa1, 0xa4, 0x11, 0x01, 0x41, 0x15, 0x4a, 0x9a, 0x11, 0x10, 0x54, 0xa1, 0xa4, 0x81, 0x01, 0x41, 0x15, 0x4a, 0x9a,
    0x18, 0x10, 0x54, 0xa1, 0xa4, 0x91, 0x01, 0x41, 0x15, 0x4a, 0x9a, 0x19, 0x10, 0x54, 0xa1, 0x00, 0x03, 0xca, 0x12, 0xa8, 0xa4, 0x51, 0x01, 0x21, 0x15, 0x0a, 0x10, 0xa1, 0xa4, 0xd1, 0x01, 0x21,
    0x15, 0x0a, 0x10, 0xa2, 0xa4, 0x31, 0x03, 0x21, 0x15, 0x0a, 0x1a, 0x10, 0x11, 0x50, 0xa1, 0xa0, 0x09, 0x11, 0x01, 0x15, 0x0a, 0x1a, 0x18, 0x11, 0x50, 0xa1, 0xa0, 0x89, 0x11, 0x01, 0x15, 0x0a,
    0x1a, 0x10, 0x12, 0x50, 0xa1, 0xa0, 0x09, 0x21, 0x01, 0x15, 0x0a, 0x1a, 0x18, 0x12, 0x50, 0xa1, 0xa0, 0x89, 0x21, 0x01, 0x15, 0x0a, 0x1a, 0x10, 0x13, 0x50, 0xa1, 0xa0, 0x09, 0x31, 0x01, 0x15,
    0x0a, 0x1a, 0x18, 0x13, 0x50, 0xa1, 0xa0, 0x89, 0x31, 0x01, 0x15, 0x0a, 0x9a, 0x13, 0x10, 0x52, 0xa1, 0xa0, 0x01, 0x01, 0x41, 0x15, 0x0a, 0x9a, 0x10, 0x10, 0x54, 0xa1, 0xa0, 0x11, 0x01, 0x41,
    0x15, 0x0a, 0x9a, 0x11, 0x10, 0x54, 0xa1, 0xa0, 0x81, 0x01, 0x41, 0x15, 0x0a, 0x9a, 0x18, 0x10, 0x54, 0xa1, 0xa0, 0x91, 0x01, 0x41, 0x15, 0x0a, 0x9a, 0x19, 0x10, 0x54, 0xa1, 0xc8, 0x01, 0x01,
    0x01, 0x15, 0x8a, 0x9c, 0x10, 0x10, 0x50, 0xa1, 0xc8, 0x81, 0x01, 0x01, 0x15, 0x8a, 0x9c, 0x18, 0x10, 0x50, 0xa1, 0xc8, 0x31, 0x03, 0x21, 0x15, 0x0a, 0xa9, 0xc8, 0x01, 0x01, 0x41, 0x15, 0x8a,
    0x9c, 0x10, 0x10, 0x54, 0xa1, 0xc8, 0x11, 0x01, 0x41, 0x15, 0x8a, 0x9c, 0x11, 0x10, 0x54, 0xa1, 0xc8, 0x81, 0x01, 0x41, 0x15, 0x8a, 0x9c, 0x18, 0x10, 0x54, 0xa1, 0xc8, 0x91, 0x01, 0x41, 0x15,
    0x8a, 0x9c, 0x19, 0x10, 0x54, 0xa1, 0xa8, 0x01, 0x01, 0x01, 0x15, 0x8a, 0x1a, 0x10, 0x10, 0x52, 0xa1, 0xa8, 0x09, 0x01, 0x21, 0x15, 0x8a, 0x1a, 0x18, 0x10, 0x52, 0xa1, 0xa8, 0x89, 0x01, 0x21,
    0x15, 0x8a, 0x1a, 0x32, 0x10, 0x54, 0x81, 0x4a, 0x09, 0x10, 0x9b, 0x43, 0x00, 0x03, 0x64, 0x0e, 0x21, 0x41, 0xe8, 0xcd, 0x41, 0x20, 0x88, 0xf2, 0x0a, 0x73, 0x10, 0x08, 0x82, 0xbc, 0xc2, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x23, 0x06, 0x08, 0x00, 0x82, 0x60, 0x10, 0xc5, 0x02, 0x28, 0x0c, 0xad, 0x80, 0x0a, 0x23, 0x06, 0x08, 0x00, 0x82, 0x60, 0x10, 0xc9, 0x42, 0x28, 0x0c, 0xae, 0x90, 0x0a,
    0x23, 0x06, 0x08, 0x00, 0x82, 0x60, 0x10, 0xcd, 0x82, 0x28, 0x0c, 0xaf, 0xa0, 0x0a, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xd4, 0x82, 0x29, 0x0c, 0xc8, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18,
    0x28, 0xb7, 0x60, 0x0a, 0xc1, 0x28, 0xa0, 0x02, 0x2a, 0xc4, 0x42, 0x2b, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x73, 0x0b, 0xa8, 0x30, 0x24, 0x23, 0x06, 0x0a, 0x00, 0x82, 0x60, 0x80, 0xe8,
    0xc2, 0x29, 0x04, 0x82, 0x2a, 0x30, 0xb0, 0x30, 0x9a, 0x10, 0x00, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xe8, 0xc2, 0x2a, 0x20, 0xcd, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xbc, 0xb0,
    0x0a, 0x41, 0x1f, 0xb4, 0x42, 0x2b, 0x08, 0xb6, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x2f, 0xb4, 0x82, 0xf2, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xe2, 0x0b, 0xad, 0x10, 0xa4,
    0xc2, 0x2b, 0xbc, 0x02, 0x2e, 0xe0, 0xc2, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xbe, 0xf0, 0x0a, 0x4c, 0x34, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x38, 0xbc, 0x42, 0xa0, 0x0a, 0xb1,
    0x10, 0x0b, 0xba, 0xa0, 0x0b, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0x80, 0x43, 0x2c, 0x38, 0xd3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xe2, 0x10, 0x0b, 0xc1, 0x2a, 0xcc, 0xc2, 0x2c,
    0xf0, 0x02, 0x2f, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x23, 0x0e, 0xb3, 0x00, 0x55, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0x90, 0xc3, 0x2c, 0x04, 0xac, 0x50, 0x0b, 0xb5, 0xe0, 0x0b,
    0xbe, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x39, 0xd4, 0x82, 0x74, 0x8d, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0x62, 0x0e, 0xb5, 0x10, 0xb4, 0xc2, 0x2d, 0xdc, 0x02, 0x38, 0x80, 0xc3,
    0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xe6, 0x70, 0x0b, 0x54, 0x36, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x3a, 0xdc, 0x42, 0xe0, 0x0a, 0xb9, 0x90, 0x0b, 0xe2, 0x20, 0x0e, 0x23, 0x06,
    0x07, 0x00, 0x82, 0x60, 0xd0, 0xa0, 0x43, 0x2e, 0x58, 0xdb, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xea, 0x90, 0x0b, 0xc1, 0x2b, 0xec, 0xc2, 0x2e, 0x90, 0x03, 0x39, 0x8c, 0x18, 0x1c, 0x00,
    0x08, 0x82, 0x41, 0xa3, 0x0e, 0xbb, 0x80, 0x75, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xb0, 0xc3, 0x2e, 0x04, 0xb0, 0xd0, 0x0b, 0xbd, 0x60, 0x0e, 0xe6, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08,
    0x06, 0x0d, 0x3b, 0xf4, 0x82, 0xf6, 0x8d, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xe2, 0x0e, 0xbd, 0x10, 0xe8, 0xc2, 0x2f, 0xfc, 0x82, 0x2b, 0x90, 0xc3, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34,
    0xee, 0xf0, 0x0b, 0x1a, 0x18, 0x8c, 0x18, 0x28, 0x00, 0x08, 0x82, 0x01, 0x12, 0x0f, 0xbe, 0x10, 0x08, 0xe1, 0x30, 0x06, 0xe7, 0x30, 0x9a, 0x10, 0x00, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0,
    0xc4, 0x83, 0x38, 0x7c, 0x64, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0xca, 0x3c, 0x88, 0x43, 0x20, 0x0b, 0xe4, 0x40, 0x0e, 0x42, 0x3b, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x33, 0x0f,
    0xe4, 0x10, 0x06, 0x66, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x4a, 0x3d, 0x90, 0x43, 0x00, 0x0e, 0xe6, 0x60, 0x0e, 0xef, 0xf0, 0x0e, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xd4, 0x83,
    0x39, 0x8c, 0x01, 0x1a, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0x72, 0x0f, 0xe6, 0x10, 0x84, 0x03, 0x3a, 0xa0, 0x43, 0x3c, 0xc4, 0xc3, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xf7, 0x80,
    0x0e, 0x65, 0xa0, 0x06, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xe4, 0x03, 0x3a, 0x04, 0xe2, 0xa0, 0x0e, 0xea, 0x30, 0x0f, 0xf3, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x4d, 0x3e, 0xa8,
    0xc3, 0x19, 0xb0, 0xc1, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xfb, 0xa0, 0x0e, 0xc1, 0x38, 0xb0, 0x03, 0x3b, 0xd4, 0x43, 0x3d, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0xb3, 0x0f, 0xec,
    0x90, 0x06, 0x6e, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x4a, 0x3f, 0xb0, 0x43, 0x40, 0x0e, 0xee, 0xe0, 0x0e, 0xf7, 0x70, 0x0f, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xf4, 0x83, 0x3b,
    0xac, 0x01, 0x1c, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xf2, 0x0f, 0xee, 0x10, 0x94, 0x03, 0x3c, 0xc0, 0x43, 0x3e, 0xe4, 0xc3, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xff, 0x00, 0x0f,
    0x6d, 0x20, 0x07, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0x84, 0x04, 0x3c, 0x04, 0xe6, 0x20, 0x0f, 0xf2, 0xb0, 0x0f, 0xfb, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x4d, 0x48, 0xc8, 0xc3,
    0x1b, 0xd0, 0xc1, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0x23, 0x21, 0x0f, 0xc1, 0x39, 0xd0, 0x03, 0x3d, 0xf4, 0x43, 0x3f, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x33, 0x12, 0xf4, 0x10,
    0x07, 0x76, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x4a, 0x49, 0xd0, 0x43, 0x10, 0x0f, 0xf6, 0x60, 0x0f, 0xe3, 0x80, 0x0e, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0x94, 0x84, 0x3d, 0xc4,
    0xc1, 0x1d, 0x8c, 0x18, 0x28, 0x00, 0x08, 0x82, 0x01, 0x82, 0x12, 0xf5, 0x10, 0x08, 0xf8, 0xa0, 0x07, 0xfe, 0x30, 0x9a, 0x10, 0x00, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xa0, 0x44, 0x3e,
    0xd8, 0xc1, 0x1e, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xa2, 0x12, 0xf9, 0x10, 0x9c, 0xc3, 0x3e, 0xec, 0x83, 0x40, 0x12, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xa8, 0xc4, 0x3e, 0xe0,
    0x41, 0x1f, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xc2, 0x12, 0xfb, 0x10, 0xdc, 0x43, 0x3f, 0xf4, 0x83, 0x49, 0x98, 0xc4, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0x2c, 0xd1, 0x0f, 0x7a,
    0xf0, 0x07, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xb8, 0x44, 0x3f, 0x04, 0xf8, 0xf0, 0x0f, 0xff, 0x80, 0x12, 0x28, 0x31, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x8d, 0x4b, 0xfc, 0x03, 0x1f,
    0x84, 0xc2, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0x30, 0xf1, 0x0f, 0x41, 0x3e, 0x84, 0x44, 0x48, 0xa8, 0x84, 0x4a, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x03, 0x13, 0x21, 0xe1, 0x07,
    0xa3, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x8a, 0x4c, 0x84, 0x44, 0xa0, 0x0f, 0x23, 0x31, 0x12, 0x2c, 0xc1, 0x12, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xc8, 0xc4, 0x48, 0x80, 0x42,
    0x29, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0x42, 0x13, 0x23, 0x11, 0xec, 0x43, 0x49, 0x94, 0x84, 0x4b, 0xb8, 0xc4, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0x34, 0x51, 0x12, 0xa2, 0x70,
    0x0a, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xd8, 0x44, 0x49, 0x04, 0xfc, 0x70, 0x12, 0x27, 0x01, 0x13, 0x30, 0x31, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x8d, 0x4d, 0x9c, 0x04, 0x29, 0xa4,
    0xc2, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0x38, 0x71, 0x12, 0x41, 0x3f, 0xa4, 0x44, 0x4a, 0xc8, 0x84, 0x4c, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x83, 0x13, 0x29, 0x61, 0x0a, 0xab,
    0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x8a, 0x4e, 0xa4, 0x44, 0xe0, 0x0f, 0x2b, 0xb1, 0x12, 0x34, 0x41, 0x13, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xe8, 0xc4, 0x4a, 0xa0, 0x42, 0x2b,
    0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xc2, 0x13, 0x2b, 0x11, 0xe0, 0x43, 0x4b, 0xb4, 0x84, 0x1f, 0xf0, 0xc1, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0x3c, 0xd1, 0x12, 0xaa, 0xf0, 0x0a,
    0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xf8, 0x44, 0x4b, 0x04, 0xf9, 0xf0, 0x12, 0x2f, 0xc1, 0x07, 0x7a, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x8d, 0x4f, 0xbc, 0x04, 0x2b, 0xc4, 0xc2,
    0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0x60, 0xf1, 0x12, 0x81, 0x3e, 0xc4, 0x44, 0x4c, 0xe8, 0x01, 0x1e, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x03, 0x16, 0x31, 0xe1, 0x0a, 0xb3, 0x30,
    0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x8a, 0x58, 0xc4, 0x44, 0xb0, 0x0f, 0x33, 0x31, 0x13, 0x78, 0x60, 0x07, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0x88, 0xc5, 0x4c, 0xc0, 0x42, 0x2d, 0x8c,
    0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0x42, 0x16, 0x33, 0x11, 0xf0, 0x43, 0x4d, 0xd4, 0xc4, 0x1b, 0xb4, 0xc1, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0x64, 0x51, 0x13, 0xb2, 0x70, 0x0b, 0x23,
    0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0x98, 0x45, 0x4d, 0x04, 0xfd, 0x70, 0x13, 0x37, 0xd1, 0x06, 0x6b, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x8d, 0x59, 0xdc, 0x04, 0x2d, 0xe4, 0xc2, 0x88,
    0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0x68, 0x71, 0x13, 0x81, 0x3f, 0xe4, 0x44, 0x4e, 0xac, 0x41, 0x1a, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x83, 0x16, 0x39, 0x61, 0x0b, 0xbb, 0x30, 0x62,
    0xb0, 0x00, 0x20, 0x08, 0x06, 0x8a, 0x5a, 0xe4, 0x44, 0xf0, 0x0f, 0x3b, 0xb1, 0x13, 0x69, 0x70, 0x06, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xa8, 0xc5, 0x4e, 0xe0, 0x42, 0x2f, 0x8c, 0x18,
    0x2c, 0x00, 0x08, 0x82, 0x81, 0xc2, 0x16, 0x3b, 0x11, 0x80, 0x44, 0x4f, 0xf4, 0x04, 0x18, 0x78, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xb0, 0x45, 0x4f, 0xe8, 0xc2, 0x2f, 0x8c, 0x18, 0x2c,
    0x00, 0x08, 0x82, 0x81, 0xe2, 0x16, 0x3d, 0x11, 0x84, 0xc4, 0x4f, 0xfc, 0x84, 0xc7, 0x8d, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0xe3, 0x16, 0x3f, 0xc1, 0x0b, 0xe1, 0x30, 0x62, 0xb0, 0x00, 0x20,
    0x08, 0x06, 0x0a, 0x5c, 0xfc, 0x44, 0x20, 0x12, 0x61, 0x11, 0x16, 0x9c, 0x36, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x5c, 0x84, 0x85, 0x2f, 0x8c, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18,
    0x28, 0x72, 0x11, 0x16, 0xc1, 0x48, 0x8c, 0xc5, 0x58, 0x68, 0xd8, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0x72, 0x31, 0x16, 0xe0, 0x50, 0x0e, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xd0,
    0xc5, 0x58, 0x04, 0x24, 0x51, 0x16, 0x65, 0xe1, 0x16, 0x6e, 0x31, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x5d, 0x94, 0x85, 0x38, 0x9c, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0x76,
    0x51, 0x16, 0x41, 0x49, 0x9c, 0xc5, 0x59, 0xc0, 0x05, 0x5c, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x63, 0x17, 0x67, 0x41, 0x0e, 0xe9, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x5e,
    0x9c, 0x45, 0x60, 0x12, 0x69, 0x91, 0x16, 0x72, 0x21, 0x17, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xe0, 0x45, 0x5a, 0x98, 0xc3, 0x3a, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xa2, 0x17,
    0x69, 0x11, 0x9c, 0xc4, 0x5a, 0xac, 0x05, 0x5d, 0xd0, 0xc5, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0x7a, 0xb1, 0x16, 0xe8, 0xd0, 0x0e, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xf0, 0xc5,
    0x5a, 0x04, 0x28, 0xd1, 0x16, 0x6d, 0x61, 0x17, 0x76, 0x31, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x5f, 0xb4, 0x85, 0x3a, 0xbc, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0x7e, 0xd1,
    0x16, 0x41, 0x4a, 0xbc, 0xc5, 0x5b, 0xe0, 0x05, 0x5e, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0xe3, 0x17, 0x6f, 0xc1, 0x0e, 0xf1, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x68, 0xbc,
    0x45, 0xa0, 0x12, 0x71, 0x11, 0x17, 0x7a, 0xa1, 0x17, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0x80, 0x46, 0x5c, 0xb8, 0xc3, 0x3c, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0x22, 0x1a, 0x71,
    0x11, 0xac, 0xc4, 0x5c, 0xcc, 0x05, 0x5f, 0xf0, 0xc5, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xa2, 0x31, 0x17, 0xf0, 0x50, 0x0f, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0x90, 0xc6, 0x5c,
    0x04, 0x2c, 0x51, 0x17, 0x75, 0xe1, 0x17, 0x7e, 0x31, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x69, 0xd4, 0x85, 0x3c, 0xdc, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xa6, 0x51, 0x17,
    0x41, 0x4b, 0xdc, 0xc5, 0x5d, 0x40, 0xce, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xa6, 0x71, 0x17, 0xf4, 0x90, 0x0f, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xa0, 0xc6, 0x5d, 0x04, 0x2e,
    0x91, 0x17, 0x79, 0xe1, 0x30, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xa0, 0x46, 0x5e, 0xd8, 0xc3, 0x3e, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xa2, 0x1a, 0x79, 0x11, 0xbc, 0xc4, 0x5e,
    0xec, 0x05, 0xa3, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0xa3, 0x1a, 0x7b, 0x81, 0x0f, 0xfd, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x6b, 0xec, 0x45, 0x00, 0x13, 0x7d, 0xd1, 0x17,
    0x0a, 0x32, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x6b, 0xf4, 0x85, 0x3e, 0xfc, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xae, 0xd1, 0x17, 0x41, 0x4c, 0xfc, 0xc5, 0x5f, 0xc0, 0x04,
    0x6a, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0xe3, 0x1a, 0x7f, 0xc1, 0x0f, 0x21, 0x31, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x6c, 0xfc, 0x45, 0x10, 0x13, 0xa1, 0x11, 0x1a, 0xaa, 0xa1,
    0x1a, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xc0, 0x46, 0x68, 0xf8, 0xc3, 0x48, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0x22, 0x1b, 0xa1, 0x11, 0xc8, 0xc4, 0x68, 0x8c, 0x06, 0x6b, 0xb0,
    0xc6, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xb2, 0x31, 0x1a, 0x20, 0x51, 0x12, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xd0, 0xc6, 0x68, 0x04, 0x33, 0x51, 0x1a, 0xa5, 0xe1, 0x1a, 0xae,
    0x31, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x6d, 0x94, 0x86, 0x48, 0x9c, 0xc4, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xb6, 0x51, 0x1a, 0x01, 0x4d, 0x9c, 0xc6, 0x69, 0xc0, 0x06, 0x6c,
    0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0x63, 0x1b, 0xa7, 0x41, 0x12, 0x29, 0x31, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x6e, 0x9c, 0x46, 0x50, 0x13, 0xa9, 0x91, 0x1a, 0xb2, 0x21, 0x1b,
    0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0xe0, 0x46, 0x6a, 0x98, 0xc4, 0x4a, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xa2, 0x1b, 0xa9, 0x11, 0xd8, 0xc4, 0x6a, 0xac, 0x06, 0x6d, 0xd0, 0xc6,
    0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x34, 0xba, 0xb1, 0x1a, 0x28, 0xd1, 0x12, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xf0, 0xc6, 0x6a, 0x04, 0x37, 0xd1, 0x1a, 0xad, 0x61, 0x1b, 0xb6, 0x31,
    0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x0d, 0x6f, 0xb4, 0x86, 0x4a, 0xbc, 0xc4, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xbe, 0xd1, 0x1a, 0x01, 0x4e, 0xbc, 0xc6, 0x6b, 0xe0, 0x06, 0x6e, 0x8c,
    0x18, 0x1c, 0x00, 0x08, 0x82, 0x41, 0xe3, 0x1b, 0xaf, 0xc1, 0x12, 0x31, 0x31, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x0a, 0x78, 0xbc, 0x46, 0x90, 0x13, 0xb1, 0x11, 0x1b, 0xba, 0x31, 0x1a, 0x23,
    0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0x80, 0x47, 0x6c, 0xb4, 0xc4, 0x4c, 0x8c, 0x18, 0x28, 0x00, 0x08, 0x82, 0x01, 0x32, 0x1e, 0xb0, 0x11, 0x08, 0xb3, 0x51, 0x13, 0xb9, 0x31, 0x9a, 0x10, 0x00,
    0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0, 0x8c, 0x07, 0x6d, 0xc4, 0x84, 0x4d, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0x52, 0x1e, 0xb4, 0x11, 0xf4, 0x84, 0x6d, 0xd8, 0x46, 0x35, 0x8d, 0x18,
    0x1c, 0x00, 0x08, 0x82, 0x41, 0x53, 0x1e, 0xb6, 0x31, 0x13, 0x38, 0x31, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0xca, 0x79, 0xd8, 0x46, 0xe0, 0x13, 0xb8, 0x81, 0x1b, 0x53, 0x34, 0x62, 0x70, 0x00,
    0x20, 0x08, 0x06, 0xcd, 0x79, 0xe0, 0x46, 0x4d, 0xe8, 0xc4, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x28, 0xe9, 0x81, 0x1b, 0xc1, 0x4f, 0xe8, 0x86, 0x6e, 0x44, 0xcf, 0x88, 0xc1, 0x01, 0x80, 0x20,
    0x18, 0x34, 0xe9, 0xa1, 0x1b, 0x37, 0xc1, 0x13, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0xa0, 0xac, 0x87, 0x6e, 0x04, 0x60, 0xc1, 0x1b, 0xbc, 0xf1, 0x34, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xd0,
    0xac, 0x07, 0x6f, 0xe4, 0x84, 0x4f, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x81, 0xd2, 0x1e, 0xbc, 0x11, 0x84, 0x85, 0x6f, 0xf8, 0x86, 0x72, 0x1e, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE cs_wmma_f32_16x16x16_fp8_quant_f16_strided_dxil = { cs_wmma_f32_16x16x16_fp8_quant_f16_strided_code_dxil, sizeof(cs_wmma_f32_16x16x16_fp8_quant_f16_strided_code_dxil) };
#undef UNUSED_ARRAY_ATTR
