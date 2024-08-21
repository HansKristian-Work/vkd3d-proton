struct Feedback
{
    uint size;
    uint data;
};

Buffer<uint> srv : register(t0);

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
    srv.GetDimensions(fb.size);
    fb.data = srv[offset].x;

    feedback[feedback_offset] = fb;
}
