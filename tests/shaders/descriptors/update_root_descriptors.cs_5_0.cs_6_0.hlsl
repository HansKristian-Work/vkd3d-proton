cbuffer cb
{
    unsigned int offset;
    unsigned int value;
};

RWByteAddressBuffer b;

[numthreads(1, 1, 1)]
void main()
{
    b.Store(4 * offset, value);
}