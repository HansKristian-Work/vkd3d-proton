static const DWORD uav_3d_sliced_view_poison_code_dxbc[] =
{
    0x43425844, 0x4c99e486, 0x7707bd40, 0xceb3b496, 0xe22f4397, 0x00000001, 0x000000b0, 0x00000003,
    0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x0000005c, 0x00050050, 0x00000017, 0x0100086a,
    0x0400289c, 0x0011e000, 0x00000000, 0x00004444, 0x0200005f, 0x00020072, 0x0400009b, 0x00000004,
    0x00000004, 0x00000010, 0x090000a4, 0x0011e0f2, 0x00000000, 0x00020a46, 0x00004002, 0x0deadca7,
    0x0deadca7, 0x0deadca7, 0x0deadca7, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE uav_3d_sliced_view_poison_dxbc = { uav_3d_sliced_view_poison_code_dxbc, sizeof(uav_3d_sliced_view_poison_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
