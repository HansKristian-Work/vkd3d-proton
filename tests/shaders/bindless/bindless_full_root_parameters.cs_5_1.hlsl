#define R(x) StructuredBuffer<uint> Buffers##x[] : register(t0, space##x)
R(0); R(1); R(2); R(3); R(4); R(5); R(6); R(7); R(8); R(9);
R(10); R(11); R(12); R(13); R(14); R(15); R(16); R(17); R(18); R(19);
R(20); R(21); R(22); R(23); R(24); R(25); R(26); R(27); R(28); R(29);
R(30); R(31); R(32); R(33); R(34); R(35); R(36); R(37); R(38); R(39);
R(40); R(41); R(42); R(43); R(44); R(45); R(46); R(47); R(48); R(49);
R(50); R(51); R(52); R(53); R(54); R(55); R(56); R(57); R(58); R(59);
R(60); R(61);
#undef R

RWByteAddressBuffer OBuffer : register(u0, space62);

[numthreads(64, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
    uint result = 0;
#define R(x) result += Buffers##x[NonUniformResourceIndex(idx)].Load(0)
    R(0); R(1); R(2); R(3); R(4); R(5); R(6); R(7); R(8); R(9);
    R(10); R(11); R(12); R(13); R(14); R(15); R(16); R(17); R(18); R(19);
    R(20); R(21); R(22); R(23); R(24); R(25); R(26); R(27); R(28); R(29);
    R(30); R(31); R(32); R(33); R(34); R(35); R(36); R(37); R(38); R(39);
    R(40); R(41); R(42); R(43); R(44); R(45); R(46); R(47); R(48); R(49);
    R(50); R(51); R(52); R(53); R(54); R(55); R(56); R(57); R(58); R(59);
    R(60); R(61);
#undef R
    OBuffer.Store(4 * idx, result);
}