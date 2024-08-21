struct Feedback
{
    uint size;
    uint data;
};

RWBuffer<uint> uav : register(u0);

RWStructuredBuffer<Feedback> feedback : register(u1);

cbuffer Args : register(b0)
{
    uint offset;
    uint data;
    uint feedback_offset;
};

[numthreads(1,1,1)]
void main()
{
    Feedback fb;
    uav.GetDimensions(fb.size);
    fb.data = uav[offset].x;

    uav[offset] = data + 1;

    feedback[feedback_offset] = fb;
}
