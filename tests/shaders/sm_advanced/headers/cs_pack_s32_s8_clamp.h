static const BYTE cs_pack_s32_s8_clamp_code_dxil[] =
{
    0x44, 0x58, 0x42, 0x43, 0xe0, 0x61, 0xd8, 0x87, 0x9d, 0x35, 0x0f, 0x14, 0x32, 0xb8, 0x6c, 0x6a, 0x49, 0x1a, 0xa8, 0x7a, 0x01, 0x00, 0x00, 0x00, 0x70, 0x07, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x78, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0xf9, 0x0a, 0xad, 0xc8, 0xaf, 0x7a, 0x5c, 0x1d, 0x5f, 0x35, 0x74,
    0x2b, 0x4f, 0x9e, 0x3c, 0x44, 0x58, 0x49, 0x4c, 0x64, 0x06, 0x00, 0x00, 0x66, 0x00, 0x05, 0x00, 0x99, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x06, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x4c, 0x06, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91,
    0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14,
    0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c,
    0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d, 0x86, 0xf0, 0xff, 0xff,
    0xff, 0xff, 0x03, 0x20, 0x01, 0xd5, 0x06, 0x62, 0xf8, 0xff, 0xff, 0xff, 0xff, 0x01, 0x90, 0x00, 0x49, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x4c, 0x08, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x4a, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14,
    0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x78, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0xa0, 0x0c, 0x63, 0x0c, 0x22, 0x73, 0x04, 0x48, 0x39, 0xc6, 0x30, 0x63, 0x8c, 0x41,
    0xe7, 0xa6, 0xe1, 0xf2, 0x27, 0xec, 0x21, 0x24, 0x7f, 0x25, 0xa4, 0x95, 0x98, 0x7c, 0xe4, 0xb6, 0x51, 0x31, 0xc6, 0x18, 0x83, 0xcc, 0x3d, 0xc3, 0xe5, 0x4f, 0xd8, 0x43, 0x48, 0x7e, 0x08, 0x34,
    0xc3, 0x42, 0xa0, 0x40, 0x95, 0x23, 0x8d, 0x35, 0x86, 0x19, 0xc4, 0xca, 0x02, 0xc6, 0x1a, 0x63, 0x8c, 0x31, 0xcc, 0x20, 0x77, 0xdb, 0x70, 0xf9, 0x13, 0xf6, 0x10, 0x92, 0xbf, 0x12, 0x92, 0x43,
    0x45, 0x02, 0x91, 0x46, 0xce, 0x43, 0x44, 0x13, 0x42, 0x48, 0x48, 0x18, 0xa3, 0x10, 0x6b, 0x2c, 0x48, 0xf1, 0xa0, 0xe1, 0xf2, 0x27, 0xec, 0x21, 0x24, 0x7f, 0x25, 0xa4, 0x0d, 0x69, 0x06, 0x44,
    0x8c, 0x31, 0xcc, 0x1c, 0x41, 0x50, 0x8a, 0x35, 0xe6, 0xa0, 0x54, 0x07, 0x02, 0x86, 0x11, 0x88, 0x61, 0xa6, 0x33, 0x18, 0x07, 0x76, 0x08, 0x87, 0x79, 0x98, 0x07, 0x37, 0x98, 0x05, 0x7a, 0x90,
    0x87, 0x7a, 0x18, 0x07, 0x7a, 0xa8, 0x07, 0x79, 0x28, 0x07, 0x72, 0x10, 0x85, 0x7a, 0x30, 0x07, 0x73, 0x28, 0x07, 0x79, 0xe0, 0x03, 0x7b, 0x28, 0x87, 0x71, 0xa0, 0x87, 0x77, 0x90, 0x07, 0x3e,
    0x48, 0x07, 0x77, 0xa0, 0x07, 0x36, 0x00, 0x03, 0x3a, 0xf0, 0x03, 0x30, 0xf0, 0x03, 0x14, 0x60, 0xca, 0x33, 0x99, 0xc1, 0x38, 0xb0, 0x43, 0x38, 0xcc, 0xc3, 0x3c, 0xb8, 0x81, 0x2c, 0xdc, 0xc2,
    0x2c, 0xd0, 0x83, 0x3c, 0xd4, 0xc3, 0x38, 0xd0, 0x43, 0x3d, 0xc8, 0x43, 0x39, 0x90, 0x83, 0x28, 0xd4, 0x83, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x03, 0x1f, 0xd4, 0x83, 0x3b, 0xcc, 0x43, 0x3a, 0x9c,
    0x83, 0x3b, 0x94, 0x03, 0x39, 0x80, 0x41, 0x3a, 0xb8, 0x03, 0x3d, 0xf8, 0x01, 0x0a, 0x06, 0xed, 0x39, 0x02, 0x50, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79,
    0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0,
    0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73,
    0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07,
    0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x04, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x0e, 0x20, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x30, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xe4, 0x71, 0x80, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
    0xc8, 0x13, 0x01, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x90, 0xa7, 0x02, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x2c, 0x10, 0x0a, 0x00, 0x00, 0x00,
    0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x1a, 0x25, 0x50, 0x0c, 0xe5, 0x30, 0x02, 0x50, 0x18, 0x05, 0x52, 0x08, 0x44, 0x47, 0x00, 0xa8, 0x17,
    0x08, 0xf1, 0x19, 0x00, 0xd2, 0x33, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x8e, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x24,
    0xc6, 0xe5, 0xc6, 0x45, 0x66, 0x06, 0x06, 0xc7, 0xe5, 0x06, 0x04, 0xc5, 0x26, 0xa7, 0xac, 0x86, 0xa6, 0x4c, 0x26, 0x07, 0x26, 0x65, 0x43, 0x10, 0x4c, 0x10, 0x86, 0x63, 0x82, 0x30, 0x20, 0x1b,
    0x84, 0x81, 0x98, 0x20, 0x0c, 0xc9, 0x06, 0x61, 0x30, 0x28, 0x8c, 0xcd, 0x6d, 0x18, 0x10, 0x82, 0x98, 0x20, 0x0c, 0xca, 0x04, 0x41, 0x8b, 0x08, 0x4c, 0x10, 0x86, 0x65, 0x82, 0x30, 0x30, 0x1b,
    0x84, 0xc1, 0xd9, 0x90, 0x28, 0x0b, 0xa3, 0x28, 0x43, 0xa3, 0x3c, 0x1b, 0x02, 0x68, 0x82, 0xc0, 0x41, 0x13, 0x04, 0xca, 0x99, 0x20, 0x0c, 0xcd, 0x06, 0x61, 0xa0, 0x36, 0x2c, 0x8a, 0xc4, 0x28,
    0xca, 0xd0, 0x4c, 0xd3, 0x54, 0x6d, 0x08, 0xac, 0x0d, 0x44, 0x74, 0x01, 0xc0, 0x04, 0x41, 0x00, 0x48, 0xb4, 0x85, 0xa5, 0xb9, 0x4d, 0x10, 0xba, 0x67, 0xc3, 0x30, 0x0c, 0xc3, 0x06, 0x42, 0xd9,
    0x28, 0x6e, 0x43, 0x91, 0x69, 0x00, 0xd6, 0x55, 0x61, 0x63, 0xb3, 0x6b, 0x73, 0x49, 0x23, 0x2b, 0x73, 0xa3, 0x9b, 0x12, 0x04, 0x55, 0xc8, 0xf0, 0x5c, 0xec, 0xca, 0xe4, 0xe6, 0xd2, 0xde, 0xdc,
    0xa6, 0x04, 0x44, 0x13, 0x32, 0x3c, 0x17, 0xbb, 0x30, 0x36, 0xbb, 0x32, 0xb9, 0x29, 0x81, 0x51, 0x87, 0x0c, 0xcf, 0x65, 0x0e, 0x2d, 0x8c, 0xac, 0x4c, 0xae, 0xe9, 0x8d, 0xac, 0x8c, 0x6d, 0x4a,
    0x80, 0x94, 0x21, 0xc3, 0x73, 0x91, 0x2b, 0x9b, 0x7b, 0xab, 0x93, 0x1b, 0x2b, 0x9b, 0x9b, 0x12, 0x5c, 0x75, 0xc8, 0xf0, 0x5c, 0xca, 0xdc, 0xe8, 0xe4, 0xf2, 0xa0, 0xde, 0xd2, 0xdc, 0xe8, 0xe6,
    0xa6, 0x04, 0x1d, 0x00, 0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07,
    0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43,
    0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76,
    0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8,
    0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68,
    0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71,
    0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4,
    0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43,
    0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x0c, 0xc4, 0x21, 0x07, 0x7c, 0x70, 0x03, 0x7a, 0x28, 0x87, 0x76, 0x80, 0x87, 0x19,
    0xd1, 0x43, 0x0e, 0xf8, 0xe0, 0x06, 0xe4, 0x20, 0x0e, 0xe7, 0xe0, 0x06, 0xf6, 0x10, 0x0e, 0xf2, 0xc0, 0x0e, 0xe1, 0x90, 0x0f, 0xef, 0x50, 0x0f, 0xf4, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x56, 0x40, 0x0d, 0x97, 0xef, 0x3c, 0x3e, 0xd0, 0x34, 0xce, 0x04, 0x4c, 0x44, 0x08, 0x34, 0xc3, 0x42, 0x98, 0x41, 0x37, 0x5c, 0xbe, 0xf3, 0xf8, 0x42, 0x44, 0x00, 0x13,
    0x11, 0x02, 0xcd, 0xb0, 0x10, 0x5f, 0xe4, 0x30, 0x1b, 0xd2, 0x0c, 0x48, 0x63, 0x98, 0x40, 0x34, 0x5c, 0xbe, 0xf3, 0xf8, 0x0f, 0x20, 0x28, 0xf8, 0xc5, 0xfb, 0xc8, 0x6d, 0xdb, 0xc0, 0x35, 0x5c,
    0xbe, 0xf3, 0xf8, 0x11, 0x60, 0x6d, 0x54, 0x51, 0x10, 0x51, 0xe9, 0x00, 0x83, 0x8f, 0xdc, 0xb6, 0x11, 0x60, 0xc3, 0xe5, 0x3b, 0x8f, 0x1f, 0x01, 0xd6, 0x46, 0x15, 0x05, 0x11, 0xb1, 0x93, 0x13,
    0x11, 0x3e, 0x72, 0xdb, 0x16, 0x20, 0x0d, 0x97, 0xef, 0x3c, 0xfe, 0x74, 0x44, 0x04, 0x30, 0x88, 0x83, 0x8f, 0xdc, 0xb6, 0x01, 0x10, 0x0c, 0x80, 0x34, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00,
    0x2a, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x34, 0x66, 0x00, 0x4a, 0x76, 0xa0, 0x60, 0x07, 0x4a, 0x37, 0xa0, 0x2c, 0x05, 0x0a, 0x77, 0xa0,
    0x30, 0x81, 0x0a, 0x53, 0x80, 0x4c, 0x09, 0x94, 0x47, 0x11, 0x10, 0x9c, 0x43, 0x58, 0x98, 0x39, 0x04, 0xac, 0xa1, 0x39, 0x07, 0xa1, 0x28, 0x8a, 0x36, 0x02, 0x00, 0x00, 0x23, 0x06, 0x08, 0x00,
    0x82, 0x60, 0x50, 0x6d, 0x8e, 0x70, 0x4d, 0x23, 0x06, 0x08, 0x00, 0x82, 0x60, 0x50, 0x71, 0x8f, 0x80, 0x51, 0x23, 0x06, 0x06, 0x00, 0x82, 0x60, 0x40, 0x88, 0x81, 0x93, 0x8d, 0x18, 0x1c, 0x00,
    0x08, 0x82, 0x41, 0xf4, 0x41, 0xc2, 0x31, 0x62, 0xa0, 0x00, 0x20, 0x08, 0x06, 0x8c, 0x18, 0x3c, 0x81, 0xb0, 0x29, 0xd8, 0x68, 0x42, 0x00, 0x8c, 0x26, 0x08, 0xc1, 0x68, 0xc2, 0x20, 0x8c, 0x26,
    0x10, 0xc3, 0x88, 0x81, 0x02, 0x80, 0x20, 0x18, 0x1c, 0x68, 0x30, 0x39, 0xc4, 0x20, 0x04, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0x10, 0x99, 0xc1, 0xa5, 0x34, 0x23, 0x06, 0x0e, 0x00, 0x82, 0x60,
    0xe0, 0xa0, 0xc1, 0x14, 0x24, 0x62, 0x20, 0x68, 0x9a, 0x26, 0x7d, 0x08, 0x00, 0x00, 0x00, 0x00,
};
static const D3D12_SHADER_BYTECODE cs_pack_s32_s8_clamp_dxil = { cs_pack_s32_s8_clamp_code_dxil, sizeof(cs_pack_s32_s8_clamp_code_dxil) };
