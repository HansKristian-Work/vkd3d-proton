RWStructuredBuffer<uint> uav : register(u1);

void main(in nointerpolation uint p : UV_PRIMITIVE_ID)
{
    uint dword_index = p >> 5;
    uint bit_index = p & 31;
    InterlockedOr(uav[dword_index], 1u << bit_index);
}
