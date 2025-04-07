static const BYTE cs_wmma_fp32_fp8_conversions_code_dxil[] =
{
    0x44, 0x58, 0x42, 0x43, 0xd0, 0xbe, 0x7f, 0x80, 0x34, 0x93, 0x29, 0xea, 0x1b, 0xbe, 0x66, 0x2d, 0x1b, 0xdd, 0xa8, 0x1d, 0x01, 0x00, 0x00, 0x00, 0x8c, 0x0e, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x0c, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x80, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xde, 0x0a, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xab, 0xdd, 0x05, 0x47,
    0x32, 0x7b, 0x6d, 0xd0, 0xd6, 0x4f, 0xc8, 0x73, 0xad, 0x2a, 0x0c, 0xc9, 0x44, 0x58, 0x49, 0x4c, 0x78, 0x0d, 0x00, 0x00, 0x66, 0x00, 0x05, 0x00, 0x5e, 0x03, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c,
    0x06, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x60, 0x0d, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x55, 0x03, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02,
    0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90,
    0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07,
    0x40, 0x02, 0xa8, 0x0d, 0x84, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x03, 0x20, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00,
    0x89, 0x20, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8a, 0x8c,
    0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x78, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0xa0, 0x08, 0x63, 0x10, 0x29, 0xc3, 0x18, 0x83, 0xcc, 0x0c, 0x40, 0x19, 0xd0, 0x18, 0x94, 0xe6, 0x08, 0x10,
    0x5a, 0xf7, 0x0c, 0x97, 0x3f, 0x61, 0x0f, 0x21, 0xf9, 0x21, 0xd0, 0x0c, 0x0b, 0x81, 0x02, 0x56, 0x16, 0x30, 0xda, 0x18, 0x63, 0x8c, 0xb1, 0x06, 0xb9, 0x82, 0x8c, 0xd1, 0xc6, 0x18, 0x63, 0x10,
    0xbc, 0x6d, 0xb8, 0xfc, 0x09, 0x7b, 0x08, 0xc9, 0x5f, 0x09, 0xc9, 0xa1, 0x22, 0x81, 0x48, 0x23, 0xe7, 0x21, 0xa2, 0x09, 0x21, 0x24, 0x24, 0x8c, 0x51, 0x88, 0x36, 0x9a, 0xa4, 0x79, 0xd0, 0x70,
    0xf9, 0x13, 0xf6, 0x10, 0x92, 0xbf, 0x12, 0xd2, 0x86, 0x34, 0x03, 0x22, 0xc6, 0x18, 0x6b, 0x8e, 0x20, 0x28, 0x45, 0x1b, 0x75, 0x58, 0xba, 0x03, 0x01, 0xa7, 0x49, 0x53, 0x44, 0x09, 0x93, 0xbf,
    0xc2, 0x1b, 0x36, 0x11, 0xda, 0x30, 0x44, 0x84, 0x24, 0x6d, 0x54, 0x51, 0x10, 0x11, 0x0a, 0x06, 0xe9, 0x39, 0x02, 0x50, 0x98, 0x02, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87,
    0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d,
    0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07,
    0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20,
    0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76,
    0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x04, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x0c, 0x20,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x24, 0x40, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xe4, 0x71, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x60, 0xc8, 0x03, 0x01, 0x01, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x90, 0x67, 0x02, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x21, 0xcf,
    0x05, 0x04, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59, 0x20, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47,
    0xc6, 0x04, 0x43, 0x1a, 0x25, 0x50, 0x0c, 0x05, 0x31, 0x02, 0x50, 0x16, 0xc5, 0xbb, 0x8a, 0xff, 0xff, 0x50, 0x08, 0x05, 0x44, 0x76, 0x04, 0x80, 0x78, 0x81, 0xd0, 0x9e, 0x01, 0x00, 0x00, 0x00,
    0x79, 0x18, 0x00, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0x44, 0x8f, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x24, 0xc6, 0x05, 0xc7, 0x45, 0x86, 0x06, 0xa6, 0xc6,
    0x25, 0xa6, 0x06, 0x04, 0xc5, 0x8c, 0xec, 0xa6, 0xac, 0x86, 0x46, 0x6c, 0x8c, 0x2c, 0x65, 0x43, 0x10, 0x4c, 0x10, 0x06, 0x64, 0x82, 0x30, 0x24, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xca, 0x06,
    0x61, 0x30, 0x28, 0x8c, 0xcd, 0x6d, 0x18, 0x10, 0x82, 0x98, 0x20, 0x0c, 0xcb, 0x04, 0x61, 0x93, 0x08, 0x4c, 0x10, 0x06, 0x66, 0x82, 0x60, 0x41, 0x1b, 0x16, 0x65, 0x61, 0x14, 0x65, 0x68, 0x1c,
    0xc7, 0x01, 0x26, 0x08, 0x43, 0xb3, 0x61, 0x19, 0x16, 0x06, 0x52, 0x86, 0xc6, 0x71, 0x1c, 0x60, 0x83, 0xf0, 0x44, 0x1b, 0x08, 0x40, 0x02, 0x80, 0x09, 0x82, 0x00, 0x90, 0x68, 0x0b, 0x4b, 0x73,
    0x9b, 0x20, 0x70, 0xd1, 0x04, 0x61, 0x70, 0x26, 0x08, 0xc3, 0xb3, 0x61, 0xc0, 0x86, 0x61, 0x43, 0x80, 0x6d, 0x30, 0x14, 0xeb, 0xca, 0x1a, 0x6d, 0x43, 0x41, 0x55, 0xc0, 0xb4, 0x55, 0x61, 0x63,
    0xb3, 0x6b, 0x73, 0x49, 0x23, 0x2b, 0x73, 0xa3, 0x9b, 0x12, 0x04, 0x55, 0xc8, 0xf0, 0x5c, 0xec, 0xca, 0xe4, 0xe6, 0xd2, 0xde, 0xdc, 0xa6, 0x04, 0x44, 0x13, 0x32, 0x3c, 0x17, 0xbb, 0x30, 0x36,
    0xbb, 0x32, 0xb9, 0x29, 0x81, 0x51, 0x87, 0x0c, 0xcf, 0x65, 0x0e, 0x2d, 0x8c, 0xac, 0x4c, 0xae, 0xe9, 0x8d, 0xac, 0x8c, 0x6d, 0x4a, 0x80, 0x94, 0x21, 0xc3, 0x73, 0x91, 0x2b, 0x9b, 0x7b, 0xab,
    0x93, 0x1b, 0x2b, 0x9b, 0x9b, 0x12, 0x48, 0x75, 0xc8, 0xf0, 0x5c, 0xca, 0xdc, 0xe8, 0xe4, 0xf2, 0xa0, 0xde, 0xd2, 0xdc, 0xe8, 0xe6, 0xa6, 0x04, 0x1b, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00,
    0x51, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6,
    0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8,
    0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11,
    0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89,
    0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37,
    0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81,
    0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c,
    0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc,
    0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x0c, 0xc4, 0x21, 0x07, 0x7c, 0x70, 0x03, 0x7a, 0x28, 0x87, 0x76, 0x80, 0x87, 0x19, 0xd1, 0x43, 0x0e, 0xf8, 0xe0, 0x06, 0xe4, 0x20,
    0x0e, 0xe7, 0xe0, 0x06, 0xf6, 0x10, 0x0e, 0xf2, 0xc0, 0x0e, 0xe1, 0x90, 0x0f, 0xef, 0x50, 0x0f, 0xf4, 0x30, 0x83, 0x81, 0xc8, 0x01, 0x1f, 0xdc, 0x40, 0x1c, 0xe4, 0xa1, 0x1c, 0xc2, 0x61, 0x1d,
    0xdc, 0x40, 0x1c, 0xe4, 0x01, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x66, 0x40, 0x0d, 0x97, 0xef, 0x3c, 0x3e, 0xd0, 0x34, 0xce, 0x04, 0x4c, 0x44, 0x08, 0x34, 0xc3,
    0x42, 0x58, 0xc1, 0x37, 0x5c, 0xbe, 0xf3, 0xf8, 0xc0, 0xe4, 0x30, 0x88, 0xc0, 0x39, 0xcc, 0x03, 0x44, 0x84, 0x77, 0x09, 0x07, 0xd0, 0x18, 0x84, 0x8f, 0xdc, 0xb6, 0x1d, 0x74, 0xc3, 0xe5, 0x3b,
    0x8f, 0x2f, 0x44, 0x04, 0x30, 0x11, 0x21, 0xd0, 0x0c, 0x0b, 0xf1, 0x45, 0x0e, 0xb3, 0x21, 0xcd, 0x80, 0x34, 0x86, 0x05, 0x88, 0xc1, 0x70, 0xf9, 0xce, 0xe3, 0x17, 0x0b, 0x30, 0x4d, 0x44, 0x43,
    0x0c, 0xed, 0x11, 0x11, 0xc0, 0x20, 0x0e, 0x62, 0x03, 0x46, 0x0e, 0xf5, 0xf8, 0xc8, 0x6d, 0x9b, 0x40, 0x34, 0x5c, 0xbe, 0xf3, 0xf8, 0x46, 0xe4, 0x50, 0x8f, 0x38, 0xf8, 0xc8, 0x6d, 0xdb, 0x00,
    0x35, 0x5c, 0xbe, 0xf3, 0xf8, 0x0b, 0x61, 0x00, 0x02, 0xf6, 0xd5, 0x7a, 0xeb, 0x7c, 0xb7, 0x6d, 0x04, 0xd8, 0x70, 0xf9, 0xce, 0xe3, 0x47, 0x80, 0xb5, 0x51, 0x45, 0x41, 0x44, 0xec, 0xe4, 0x44,
    0x84, 0x8f, 0xdc, 0xb6, 0x01, 0x10, 0x0c, 0x80, 0x34, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0xfd, 0x01, 0x00, 0x00, 0x13, 0x04, 0x43, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00,
    0x34, 0x66, 0x00, 0x0a, 0x76, 0xa0, 0x3c, 0x03, 0x4a, 0x76, 0xa0, 0x0c, 0x05, 0xca, 0x1c, 0x33, 0x10, 0x50, 0xa1, 0xcc, 0x01, 0x01, 0x21, 0x15, 0xca, 0x9c, 0x10, 0x10, 0x52, 0xa1, 0xcc, 0x11,
    0x01, 0x21, 0x15, 0xca, 0x9c, 0x11, 0x10, 0x52, 0xa1, 0xcc, 0x81, 0x01, 0x21, 0x15, 0xca, 0x9c, 0x18, 0x10, 0x52, 0xa1, 0xcc, 0x91, 0x01, 0x21, 0x15, 0xca, 0x9c, 0x19, 0x10, 0x52, 0xa1, 0xc8,
    0x01, 0x01, 0x01, 0x15, 0x8a, 0x9c, 0x10, 0x10, 0x50, 0xa1, 0xc8, 0x81, 0x01, 0x01, 0x15, 0x8a, 0x9c, 0x18, 0x10, 0x50, 0xa1, 0xc8, 0x31, 0x03, 0x21, 0x15, 0x8a, 0xaa, 0xc8, 0x01, 0x01, 0x41,
    0x15, 0x8a, 0x9c, 0x10, 0x10, 0x54, 0xa1, 0xc8, 0x11, 0x01, 0x41, 0x15, 0x8a, 0x9c, 0x11, 0x10, 0x54, 0xa1, 0xc8, 0x81, 0x01, 0x41, 0x15, 0x8a, 0x9c, 0x18, 0x10, 0x54, 0xa1, 0xc8, 0x91, 0x01,
    0x41, 0x15, 0x8a, 0x9c, 0x19, 0x10, 0x54, 0xa1, 0xc0, 0x01, 0x01, 0x01, 0x15, 0x0a, 0x9c, 0x10, 0x10, 0x50, 0xa1, 0xc0, 0x81, 0x01, 0x01, 0x15, 0x0a, 0x9c, 0x18, 0x10, 0x50, 0xa1, 0xc0, 0x51,
    0x03, 0x21, 0x15, 0x0a, 0x38, 0xa0, 0x78, 0x03, 0xca, 0x12, 0xa8, 0x00, 0x01, 0x09, 0xca, 0xa0, 0x30, 0x05, 0x68, 0x95, 0x40, 0x19, 0x90, 0x9c, 0x43, 0xb0, 0x83, 0x85, 0xea, 0x1c, 0xc4, 0xb2,
    0x34, 0x7a, 0x30, 0x07, 0xb1, 0x2c, 0x8b, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x23, 0x06, 0x08, 0x00, 0x82, 0x60, 0x70, 0xe1, 0x41, 0x1a, 0x08, 0x74, 0xf0, 0x06, 0x23, 0x06, 0x08, 0x00, 0x82, 0x60,
    0x70, 0xe5, 0x81, 0x1a, 0x08, 0x75, 0x00, 0x07, 0x23, 0x06, 0x05, 0x00, 0x82, 0x60, 0x40, 0x80, 0x42, 0x33, 0x62, 0x60, 0x00, 0x20, 0x08, 0x06, 0x06, 0x28, 0x34, 0x77, 0x30, 0x62, 0x60, 0x00,
    0x20, 0x08, 0x06, 0x09, 0x28, 0xb0, 0x41, 0x70, 0xc1, 0xd8, 0x11, 0x83, 0x03, 0x00, 0x41, 0x30, 0x98, 0xfe, 0x20, 0x0e, 0x8c, 0x64, 0xc4, 0x60, 0x01, 0x40, 0x10, 0x0c, 0xa0, 0x50, 0x88, 0x83,
    0xc0, 0x0d, 0xe6, 0x60, 0x0e, 0x84, 0x3d, 0x18, 0x31, 0x38, 0x00, 0x10, 0x04, 0x83, 0x29, 0x14, 0xe6, 0x00, 0x59, 0x46, 0x0c, 0x16, 0x00, 0x04, 0xc1, 0x00, 0x1a, 0x85, 0x39, 0x08, 0xde, 0xa0,
    0x0e, 0xea, 0xa0, 0x0f, 0xfa, 0x60, 0xc4, 0xe0, 0x00, 0x40, 0x10, 0x0c, 0xa6, 0x51, 0xa8, 0x03, 0xa5, 0x19, 0x31, 0x58, 0x00, 0x10, 0x04, 0x03, 0xa8, 0x14, 0xea, 0x20, 0x80, 0x83, 0x3b, 0xb8,
    0x83, 0x3f, 0xf8, 0x83, 0x11, 0x83, 0x03, 0x00, 0x41, 0x30, 0x98, 0x4a, 0xe1, 0x0e, 0x98, 0x67, 0xc4, 0x60, 0x01, 0x40, 0x10, 0x0c, 0xa0, 0x53, 0xb8, 0x83, 0x20, 0x0e, 0xf2, 0x20, 0x0f, 0x42,
    0x21, 0x14, 0x46, 0x0c, 0x0e, 0x00, 0x04, 0xc1, 0x60, 0x3a, 0x85, 0x3c, 0x70, 0xa2, 0x11, 0x83, 0x05, 0x00, 0x41, 0x30, 0x80, 0x52, 0x21, 0x0f, 0x02, 0x39, 0xd8, 0x83, 0x3d, 0x18, 0x85, 0x51,
    0x18, 0x31, 0x38, 0x00, 0x10, 0x04, 0x83, 0x29, 0x15, 0xf6, 0x00, 0x9a, 0x46, 0x0c, 0x16, 0x00, 0x04, 0xc1, 0x00, 0x5a, 0x85, 0x3d, 0x08, 0xe6, 0xa0, 0x0f, 0xfa, 0xa0, 0x14, 0x4a, 0x61, 0xc4,
    0xe0, 0x00, 0x40, 0x10, 0x0c, 0xa6, 0x55, 0xe8, 0x03, 0xa9, 0x1a, 0x31, 0x58, 0x00, 0x10, 0x04, 0x03, 0xa8, 0x15, 0xfa, 0x20, 0xa0, 0x83, 0x3f, 0xf8, 0x83, 0x53, 0x38, 0x85, 0x11, 0x83, 0x03,
    0x00, 0x41, 0x30, 0x98, 0x5a, 0xe1, 0x0f, 0xa8, 0x6b, 0xc4, 0x60, 0x01, 0x40, 0x10, 0x0c, 0xa0, 0x57, 0xf8, 0x83, 0xa0, 0x0e, 0x42, 0x21, 0x14, 0x52, 0x21, 0x15, 0x46, 0x0c, 0x0e, 0x00, 0x04,
    0xc1, 0x60, 0x7a, 0x85, 0x50, 0xb0, 0xb2, 0x11, 0x83, 0x05, 0x00, 0x41, 0x30, 0x80, 0x62, 0x21, 0x14, 0x02, 0x3b, 0x18, 0x85, 0x51, 0x58, 0x85, 0x55, 0xa8, 0x0a, 0x0c, 0x76, 0xc4, 0xc0, 0x00,
    0x40, 0x10, 0x0c, 0x92, 0x5a, 0x08, 0x85, 0xe0, 0x82, 0xb1, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0x30, 0xd1, 0x82, 0x29, 0x6c, 0xde, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0xb6, 0x60, 0x0a,
    0xc1, 0x28, 0xa0, 0x02, 0x2a, 0x08, 0xb0, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x93, 0x2d, 0xa0, 0x42, 0x07, 0x06, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xe1, 0x02, 0x2a, 0x04, 0xa4,
    0xa0, 0x0a, 0xaa, 0x20, 0x0b, 0xb2, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x13, 0x2e, 0xa8, 0xc2, 0x27, 0x06, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xe9, 0x82, 0x2a, 0x04, 0xa5, 0xc0,
    0x0a, 0xac, 0x40, 0x0b, 0xb4, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x93, 0x2e, 0xb0, 0x42, 0x18, 0x90, 0xc1, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0xbc, 0xc0, 0x0a, 0x81, 0x29, 0xb8,
    0x82, 0x2b, 0xd8, 0x82, 0x2d, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0xc1, 0xc4, 0x0b, 0xae, 0x30, 0x06, 0x66, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x90, 0x2f, 0xb8, 0x42, 0x70, 0x0a, 0xb0,
    0x00, 0x0b, 0xb8, 0x80, 0x0b, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0x30, 0xf9, 0x02, 0x2c, 0x94, 0x01, 0x1a, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0x04, 0x0e, 0xb0, 0x10, 0xa0, 0x82, 0x2c,
    0xc8, 0x82, 0x2e, 0xe8, 0xc2, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x4c, 0xe0, 0x20, 0x0b, 0x67, 0xa0, 0x06, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0x89, 0x83, 0x2c, 0x04, 0xa9, 0x40, 0x0b,
    0xb4, 0xc0, 0x0b, 0xbc, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x93, 0x38, 0xd0, 0x42, 0x1a, 0xb0, 0xc1, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0xe4, 0x40, 0x0b, 0x81, 0x2a, 0xd8, 0x82,
    0x2d, 0xf8, 0x82, 0x2f, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0xc1, 0x44, 0x0e, 0xb6, 0xb0, 0x06, 0x6e, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x90, 0x39, 0xd8, 0x42, 0xb0, 0x0a, 0xb8, 0x80,
    0x0b, 0xe0, 0x00, 0x0e, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0x30, 0x99, 0x03, 0x2e, 0xb4, 0x01, 0x1c, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0x84, 0x0e, 0xb8, 0x10, 0xb0, 0x82, 0x2e, 0xe8,
    0x42, 0x19, 0x8c, 0xc1, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x4c, 0xe8, 0xa0, 0x0b, 0x6f, 0x20, 0x07, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xa9, 0x83, 0x2e, 0x04, 0xad, 0xc0, 0x0b, 0xbc,
    0x30, 0x06, 0x61, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x93, 0x3a, 0xf0, 0x42, 0x1c, 0xd0, 0xc1, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0xec, 0xc0, 0x0b, 0x81, 0x2b, 0xf8, 0x82, 0x2f,
    0x84, 0xc1, 0x37, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x13, 0x3b, 0xf8, 0xc2, 0x1c, 0xd8, 0xc1, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0xee, 0xe0, 0x0b, 0xc1, 0x2b, 0x80, 0x03, 0x38, 0x7c,
    0xdd, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x4c, 0xee, 0x00, 0x0e, 0x75, 0x80, 0x07, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xc1, 0x03, 0x38, 0x04, 0xb0, 0x20, 0x0e, 0xe2, 0xf0, 0x0a, 0xed,
    0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x13, 0x3c, 0x88, 0xc3, 0x1d, 0xe8, 0xc1, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0xf2, 0x20, 0x0e, 0x01, 0x2c, 0x90, 0x03, 0x39, 0xb0, 0x03, 0x3b,
    0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0xc1, 0x24, 0x0f, 0xe4, 0x90, 0x07, 0x7c, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x3d, 0x90, 0x43, 0x10, 0x0b, 0xe6, 0x60, 0x0e, 0xee, 0xe0, 0x0e,
    0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0x30, 0xd1, 0x83, 0x39, 0xec, 0x81, 0x1f, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0x64, 0x0f, 0xe6, 0x10, 0xc8, 0x02, 0x3a, 0xa0, 0x03, 0x3c, 0xc0, 0xc3,
    0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x4c, 0xf6, 0x80, 0x0e, 0x7d, 0x00, 0x0a, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xe1, 0x03, 0x3a, 0x04, 0xb3, 0xa0, 0x0e, 0xea, 0x20, 0x0f, 0xf2, 0x30,
    0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x13, 0x3e, 0xa8, 0xc3, 0x1f, 0x88, 0xc2, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0xfa, 0xa0, 0x0e, 0x01, 0x2d, 0xb0, 0x03, 0x3b, 0xd0, 0x03, 0x3d, 0x8c,
    0x18, 0x1c, 0x00, 0x08, 0x82, 0xc1, 0xa4, 0x0f, 0xec, 0x10, 0x0a, 0xa4, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x3f, 0xb0, 0x43, 0x50, 0x0b, 0xee, 0xe0, 0x0e, 0xf6, 0x60, 0x0f, 0x23,
    0x06, 0x07, 0x00, 0x82, 0x60, 0x30, 0xf1, 0x83, 0x3b, 0x8c, 0x82, 0x29, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0xe4, 0x0f, 0xee, 0x10, 0xd8, 0x02, 0x3c, 0xc0, 0x03, 0x3e, 0xe0, 0xc3, 0x88,
    0xc1, 0x01, 0x80, 0x20, 0x18, 0x4c, 0xfe, 0x00, 0x0f, 0xa5, 0x80, 0x0a, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0x81, 0x04, 0x3c, 0x04, 0xb7, 0x20, 0x0f, 0xf2, 0xa0, 0x0f, 0xfa, 0x30, 0x62,
    0x70, 0x00, 0x20, 0x08, 0x06, 0x13, 0x48, 0xc8, 0xc3, 0x29, 0xa8, 0xc2, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0x22, 0x21, 0x0f, 0x81, 0x39, 0xd0, 0x03, 0x3d, 0xa8, 0x01, 0x1a, 0x8c, 0x18,
    0x1c, 0x00, 0x08, 0x82, 0xc1, 0x24, 0x12, 0xf4, 0x90, 0x0a, 0xac, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x49, 0xd0, 0x43, 0x70, 0x0e, 0xf6, 0x60, 0x0f, 0x68, 0x60, 0x06, 0x23, 0x06,
    0x07, 0x00, 0x82, 0x60, 0x30, 0x91, 0x84, 0x3d, 0xac, 0x82, 0x2b, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0x64, 0x12, 0xf6, 0x10, 0xa0, 0x03, 0x3e, 0xe0, 0x83, 0x19, 0x90, 0xc1, 0x88, 0xc1,
    0x01, 0x80, 0x20, 0x18, 0x4c, 0x26, 0x81, 0x0f, 0xad, 0x00, 0x0b, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xa1, 0x04, 0x3e, 0x04, 0xe9, 0xa0, 0x0f, 0xfa, 0x40, 0x06, 0x62, 0x30, 0x62, 0x70,
    0x00, 0x20, 0x08, 0x06, 0x13, 0x4a, 0xe8, 0xc3, 0x2b, 0xc8, 0xc2, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0x2a, 0xa1, 0x0f, 0x81, 0x3a, 0xf0, 0x03, 0x3f, 0xa4, 0xc3, 0x49, 0x8c, 0x18, 0x1c,
    0x00, 0x08, 0x82, 0xc1, 0xa4, 0x12, 0xfc, 0x10, 0x0b, 0xb4, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x4b, 0xf0, 0x43, 0xa0, 0x0e, 0xfe, 0xe0, 0x0f, 0x26, 0x61, 0x12, 0x23, 0x06, 0x07,
    0x00, 0x82, 0x60, 0x30, 0xb1, 0x84, 0x3f, 0xcc, 0x82, 0x2d, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0xe4, 0x12, 0xfe, 0x10, 0xac, 0x03, 0x48, 0x80, 0x04, 0x4a, 0xa0, 0xc4, 0x88, 0xc1, 0x01,
    0x80, 0x20, 0x18, 0x4c, 0x2e, 0x01, 0x12, 0xb5, 0x80, 0x0b, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xc1, 0x04, 0x48, 0x04, 0xec, 0x20, 0x12, 0x22, 0xa1, 0x12, 0x2a, 0x31, 0x62, 0x70, 0x00,
    0x20, 0x08, 0x06, 0x13, 0x4c, 0x88, 0xc4, 0x2d, 0xe8, 0xc2, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0x32, 0x21, 0x12, 0x41, 0x3b, 0x90, 0x04, 0x49, 0xb0, 0x04, 0x4b, 0x8c, 0x18, 0x1c, 0x00,
    0x08, 0x82, 0xc1, 0x24, 0x13, 0x24, 0x91, 0x0b, 0xbc, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x4d, 0x90, 0x44, 0xe0, 0x0e, 0x26, 0x61, 0x12, 0x2e, 0xe1, 0x12, 0x23, 0x06, 0x07, 0x00,
    0x82, 0x60, 0x30, 0xd1, 0x84, 0x49, 0xec, 0x82, 0x2f, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0x64, 0x13, 0x26, 0x11, 0xbc, 0x03, 0x4a, 0xa0, 0x04, 0x4c, 0xc0, 0xc4, 0x88, 0xc1, 0x01, 0x80,
    0x20, 0x18, 0x4c, 0x36, 0x81, 0x12, 0xbd, 0x00, 0x0e, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xe1, 0x04, 0x4a, 0x04, 0xf0, 0xa0, 0x12, 0x2a, 0x21, 0x13, 0x32, 0x31, 0x62, 0x70, 0x00, 0x20,
    0x08, 0x06, 0x13, 0x4e, 0xa8, 0xc4, 0x2f, 0x88, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0x3a, 0xa1, 0x12, 0x41, 0x3c, 0xb0, 0x04, 0x4b, 0xd0, 0x04, 0x4d, 0x8c, 0x18, 0x1c, 0x00, 0x08,
    0x82, 0xc1, 0xa4, 0x13, 0x2c, 0x11, 0x0e, 0xe4, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x4f, 0xb0, 0x44, 0x20, 0x0f, 0x2e, 0xe1, 0x12, 0x6a, 0x80, 0x06, 0x23, 0x06, 0x07, 0x00, 0x82,
    0x60, 0x30, 0xf1, 0x84, 0x4b, 0x8c, 0x83, 0x39, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0xe4, 0x13, 0x2e, 0x11, 0xcc, 0x03, 0x4c, 0xc0, 0x04, 0x1a, 0x98, 0xc1, 0x88, 0xc1, 0x01, 0x80, 0x20,
    0x18, 0x4c, 0x3e, 0x01, 0x13, 0xe5, 0x80, 0x0e, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0x81, 0x05, 0x4c, 0x04, 0xf4, 0x20, 0x13, 0x32, 0x61, 0x06, 0x64, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08,
    0x06, 0x13, 0x58, 0xc8, 0xc4, 0x39, 0xa8, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0x62, 0x21, 0x13, 0x41, 0x3d, 0xd0, 0x04, 0x4d, 0x90, 0x81, 0x18, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82,
    0xc1, 0x24, 0x16, 0x34, 0x91, 0x0e, 0xec, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x59, 0xd0, 0x44, 0x60, 0x0f, 0x36, 0x61, 0x13, 0x3e, 0xe1, 0x13, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60,
    0x30, 0x91, 0x85, 0x4d, 0xac, 0x83, 0x3b, 0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0x64, 0x16, 0x36, 0x11, 0xf0, 0x03, 0x4e, 0xe0, 0x84, 0x86, 0x8d, 0x18, 0x1c, 0x00, 0x08, 0x82, 0xc1, 0x64,
    0x16, 0x38, 0xd1, 0x0e, 0xf0, 0x30, 0x62, 0xb0, 0x00, 0x20, 0x08, 0x06, 0x10, 0x5a, 0xe0, 0x44, 0xd0, 0x0f, 0x3a, 0xa1, 0x13, 0x98, 0x35, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x13, 0x5a, 0xe8,
    0xc4, 0x3b, 0xc8, 0xc3, 0x88, 0xc1, 0x02, 0x80, 0x20, 0x18, 0x40, 0x6a, 0xa1, 0x13, 0x81, 0x3f, 0xf0, 0x04, 0x4f, 0x58, 0xd4, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x4c, 0x6a, 0xc1, 0x13, 0xf1,
    0x40, 0x0f, 0x23, 0x06, 0x0b, 0x00, 0x82, 0x60, 0x00, 0xb1, 0x05, 0x4f, 0x04, 0xff, 0xe0, 0x13, 0x3e, 0x41, 0x49, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0x30, 0xb1, 0x85, 0x4f, 0xcc, 0x83, 0x3d,
    0x8c, 0x18, 0x2c, 0x00, 0x08, 0x82, 0x01, 0xe4, 0x16, 0x3e, 0x11, 0x80, 0x04, 0x58, 0x80, 0x05, 0x5a, 0xa0, 0xc5, 0x70, 0xc3, 0x3c, 0xa4, 0x05, 0x18, 0xcc, 0x32, 0x04, 0x42, 0x50, 0xf3, 0xd0,
    0x0f, 0x37, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0x13, 0x5c, 0x88, 0x85, 0x3d, 0xe8, 0xc3, 0x88, 0x81, 0x03, 0x80, 0x20, 0x18, 0x38, 0x73, 0xe1, 0x0f, 0x81, 0x40, 0x16, 0x0e, 0x41, 0x16, 0x64,
    0xc1, 0x0f, 0x69, 0x31, 0x4b, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE cs_wmma_fp32_fp8_conversions_dxil = { cs_wmma_fp32_fp8_conversions_code_dxil, sizeof(cs_wmma_fp32_fp8_conversions_code_dxil) };
#undef UNUSED_ARRAY_ATTR
