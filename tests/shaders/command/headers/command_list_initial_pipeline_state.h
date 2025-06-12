static const DWORD command_list_initial_pipeline_state_code_dxbc[] =
{
    0x43425844, 0x2f09e5ff, 0xaa135d5e, 0x7860f4b5, 0x5c7b8cbc, 0x00000001, 0x000000b4, 0x00000003,
    0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
    0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
    0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000003c, 0x00000050, 0x0000000f,
    0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
    0x00000000, 0x3e800000, 0x3f000000, 0x3f800000, 0x0100003e,
};
#ifdef __GNUC__
#define UNUSED_ARRAY_ATTR __attribute__((unused))
#else
#define UNUSED_ARRAY_ATTR
#endif
UNUSED_ARRAY_ATTR static const D3D12_SHADER_BYTECODE command_list_initial_pipeline_state_dxbc = { command_list_initial_pipeline_state_code_dxbc, sizeof(command_list_initial_pipeline_state_code_dxbc) };
#undef UNUSED_ARRAY_ATTR
