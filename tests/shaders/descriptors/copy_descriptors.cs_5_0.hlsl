struct data
{
    uint3 u;
    float f;
};

cbuffer cb0
{
    float f;
};

cbuffer cb1
{
    uint u;
};

cbuffer cb2
{
    int i;
};

SamplerState s0;
SamplerState s1;
SamplerState s2;
SamplerComparisonState s3;

Texture2D t0;
Texture2D<uint> t1;
Texture2D<int> t2;
Buffer<float> t3;
StructuredBuffer<float> t4;
ByteAddressBuffer t5;
Texture2D t6;

RWByteAddressBuffer u0;
RWStructuredBuffer<data> u1;

RWByteAddressBuffer u2;

[numthreads(1, 1, 1)]
void main()
{
    u2.Store(0 * 4, f);
    u2.Store(1 * 4, u);
    u2.Store(2 * 4, i);
    u2.Store(3 * 4, 0);

    u2.Store4( 4 * 4, t0.SampleLevel(s0, (float2)0, 0));
    u2.Store4( 8 * 4, t0.SampleLevel(s1, (float2)0, 0));
    u2.Store4(12 * 4, t0.SampleLevel(s2, (float2)0, 0));

    u2.Store(16 * 4, t1.Load((int3)0));
    u2.Store(17 * 4, t2.Load((int3)0));
    u2.Store(18 * 4, t3.Load(0));
    u2.Store(19 * 4, t4[0]);

    u2.Store4(20 * 4, t5.Load4(0));

    u2.Store4(24 * 4, t6.SampleCmpLevelZero(s3, (float2)0, 0.6f));
    u2.Store4(28 * 4, t6.SampleCmpLevelZero(s3, (float2)0, 0.4f));

    u2.Store2(32 * 4, u0.Load2(0));
    u2.Store2(34 * 4, u0.Load2(8));

    u2.Store3(36 * 4, u1[0].u);
    u2.Store4(39 * 4, u1[0].f);

    u2.Store(43 * 4, 0xdeadbeef);
}