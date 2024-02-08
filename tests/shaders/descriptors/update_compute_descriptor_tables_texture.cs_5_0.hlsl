uint offset;

RWByteAddressBuffer u0 : register(u0);

cbuffer cb0 : register(b0)
{
    uint4 srv_size[2];
    uint4 uav_size[2];
};

Texture2D<uint> t0 : register(t0);
Texture2D<uint> t1 : register(t1);

RWBuffer<uint> u4 : register(u4);
RWTexture2D<uint> u6 : register(u6);

[numthreads(1, 1, 1)]
void main()
{
    uint x, y, result, byte_offset = offset;

    for (y = 0, result = 0; y < srv_size[0].y; ++y)
    {
        for (x = 0; x < srv_size[0].x; ++x)
            result += t0.Load(int3(x, y, 0));
    }
    u0.Store(byte_offset, result);
    byte_offset += 4;

    for (y = 0, result = 0; y < srv_size[1].y; ++y)
    {
        for (x = 0; x < srv_size[1].x; ++x)
            result += t1.Load(int3(x, y, 0));
    }
    u0.Store(byte_offset, result);
    byte_offset += 4;

    for (x = 0, result = 0; x < uav_size[0].x; ++x)
        result += u4[x];
    u0.Store(byte_offset, result);
    byte_offset += 4;

    for (y = 0, result = 0; y < uav_size[1].y; ++y)
    {
        for (x = 0; x < uav_size[1].x; ++x)
            result += u6[uint2(x, y)];
    }
    u0.Store(byte_offset, result);
}