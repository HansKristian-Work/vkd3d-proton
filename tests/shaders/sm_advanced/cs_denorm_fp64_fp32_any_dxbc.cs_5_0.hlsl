RWByteAddressBuffer RWBuf : register(u0);
ByteAddressBuffer ROBuf : register(t0);

[numthreads(1, 1, 1)]
void main()
{
    {
        uint4 loaded_v = ROBuf.Load4(0);
        double2 v = double2(asdouble(loaded_v.x, loaded_v.y), asdouble(loaded_v.z, loaded_v.w));
        precise double v2 = v.x + v.y;
        asuint(v2, loaded_v.x, loaded_v.y);
        RWBuf.Store2(0, loaded_v.xy);
    }

    {
        float2 v = ROBuf.Load2(16);
        precise float v2 = v.x + v.y;
        RWBuf.Store(8, v2);
    }
}
