RWByteAddressBuffer o;

uint v;

[numthreads(1, 1, 1)]
void main()
{
    o.Store(0, v);
}