/* uint4 to work around what looks like an fxc bug */
RWBuffer<uint4> buf : register(u1);
RWByteAddressBuffer uav : register(u0);

cbuffer args : register(b0)
{
    uint stride;
};

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DISPATCHTHREADID)
{
    uint fb;
    uint v = buf.Load(tid.x * stride, fb).r;
    uint s = CheckAccessFullyMapped(fb) ? 1 : 0;
    uav.Store2(8 * tid.x, uint2(v, s));
}