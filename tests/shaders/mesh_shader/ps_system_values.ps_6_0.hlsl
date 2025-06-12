RWStructuredBuffer<uint> uav : register(u1);

float4 main(in nointerpolation float4 color : UV_COLOR,
        in nointerpolation uint prim_id : SV_PRIMITIVEID) : SV_TARGET0
{
    uint dword_index = prim_id >> 5;
    uint bit_index = prim_id & 31;
    InterlockedOr(uav[dword_index], 1u << bit_index);
    return color;
}
