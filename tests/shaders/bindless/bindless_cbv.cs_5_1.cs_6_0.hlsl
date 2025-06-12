struct Foo
{
    uint value;
};
ConstantBuffer<Foo> CBVs[] : register(b2, space1);

RWByteAddressBuffer RWBuf : register(u0);

[numthreads(64, 1, 1)]
void main(uint index : SV_DispatchThreadID)
{
    uint value = CBVs[NonUniformResourceIndex(index)].value;
    RWBuf.Store(4 * index, value);
}