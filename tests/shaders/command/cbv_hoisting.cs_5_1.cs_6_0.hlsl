// Can be hoisted.
cbuffer cbuf0 : register(b0)
{
    uint v0;
};

struct C { uint v; };
// Can be hoisted.
ConstantBuffer<C> cbuf1[1]: register(b1);
// Cannot be hoisted.
ConstantBuffer<C> cbufs[2] : register(b2);

RWByteAddressBuffer RWBuf : register(u0);

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
    uint wval;
    if (thr == 0)
        wval = v0;
    else if (thr == 2)
        wval = cbufs[0].v;
    else if (thr == 3)
        wval = cbufs[1].v;
    else
    {
        // Verify that we can convert this to a plain descriptor, even with weird indexing into array size of 1.
        // Array size of 1 means we have to access one descriptor.
        wval = cbuf1[NonUniformResourceIndex(thr - 1)].v;
    }

    RWBuf.Store(4 * thr, wval);
}