ByteAddressBuffer t0;
ByteAddressBuffer t4 : register(t4);

RWByteAddressBuffer u0;
RWByteAddressBuffer u2 : register(u2);

uint size;
uint size2;

[numthreads(1, 1, 1)]
void main()
{
    uint i;
    for (i = 0; i < size; ++i)
        u0.Store(4 * i, t0.Load(4 *i));
    for (i = 0; i < size2; ++i)
        u2.Store(4 * i, t4.Load(4 * i));
}