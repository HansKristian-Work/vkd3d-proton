uint offset;

RWByteAddressBuffer u0 : register(u0);

cbuffer cb0 : register(b0)
{
    uint4 srv_size[2];
    uint4 uav_size[2];
};

Buffer<uint> t0 : register(t0);
Buffer<uint> t1 : register(t1);

RWBuffer<uint> u4 : register(u4);
RWBuffer<uint> u7 : register(u7);

[numthreads(1, 1, 1)]
void main()
{
    uint x, result, byte_offset = offset;

    for (x = 0, result = 0; x < srv_size[0].x; ++x)
        result += t0.Load(x);
    u0.Store(byte_offset, result);
    byte_offset += 4;

    for (x = 0, result = 0; x < srv_size[1].x; ++x)
        result += t1.Load(x);
    u0.Store(byte_offset, result);
    byte_offset += 4;

    for (x = 0, result = 0; x < uav_size[0].x; ++x)
        result += u4[x];
    u0.Store(byte_offset, result);
    byte_offset += 4;

    for (x = 0, result = 0; x < uav_size[1].x; ++x)
        result += u7[x];
    u0.Store(byte_offset, result);
}