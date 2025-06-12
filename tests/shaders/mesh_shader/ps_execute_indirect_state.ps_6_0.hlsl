RWStructuredBuffer<uint> Buf : register(u0);

void main()
{
    uint o;
    InterlockedAdd(Buf[0], 1, o);
}
