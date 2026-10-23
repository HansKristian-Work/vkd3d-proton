cbuffer cbv : register(b0)
{
    uint4 cbv_data[16];
};

struct structured_data_t
{
    uint4 a;
    uint2 b;
    uint2 c;
};

StructuredBuffer<structured_data_t> srv_structured : register(t0);
ByteAddressBuffer srv_raw : register(t1);

RWStructuredBuffer<structured_data_t> uav_structured : register(u0);
RWByteAddressBuffer uav_raw : register(u1);

[numthreads(1, 1, 1)]
void main()
{
    uint4 cbv_u4 = cbv_data[0];
    uint2 cbv_u2 = cbv_data[1].zw;
    uint cbv_u1 = cbv_data[2].y;

    uint4 srv_u4 = srv_structured[0].a;
    uint2 srv_u2 = srv_structured[1].c;
    uint srv_u1 = srv_structured[1].b.y;

    uint4 raw_u4 = srv_raw.Load4(0);
    uint2 raw_u2 = srv_raw.Load2(16);
    uint raw_u1 = srv_raw.Load(32);

    uav_structured[0].a = cbv_u4;
    uav_structured[1].b = cbv_u2;
    uav_structured[2].c.x = cbv_u1;

    uav_structured[3].a = srv_u4;
    uav_structured[4].b = srv_u2;
    uav_structured[5].c.x = srv_u1;

    uav_structured[6].a = raw_u4;
    uav_structured[7].b = raw_u2;
    uav_structured[8].c.x = raw_u1;

    uav_raw.Store4(0, cbv_u4);
    uav_raw.Store2(16, cbv_u2);
    uav_raw.Store(32, cbv_u1);

    uav_raw.Store4(48, srv_u4);
    uav_raw.Store2(64, srv_u2);
    uav_raw.Store(80, srv_u1);

    uav_raw.Store4(96, raw_u4);
    uav_raw.Store2(112, raw_u2);
    uav_raw.Store(128, raw_u1);
}
