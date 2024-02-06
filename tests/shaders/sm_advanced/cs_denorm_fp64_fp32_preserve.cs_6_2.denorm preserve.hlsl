RWByteAddressBuffer RWBuf : register(u0);
ByteAddressBuffer ROBuf : register(t0);

[numthreads(1, 1, 1)]
void main()
{
    {
        double2 v = ROBuf.Load<double2>(0);
        precise double v2 = v.x + v.y;
        RWBuf.Store<double>(0, v2);
    }

    {
        float2 v = ROBuf.Load<float2>(16);
        precise float v2 = v.x + v.y;
        RWBuf.Store<float>(8, v2);
    }
}
