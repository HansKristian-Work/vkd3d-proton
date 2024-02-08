RWBuffer<uint> RWBufs[] : register(u0);
Buffer<uint> Bufs[] : register(t0);

cbuffer Params : register(b0) {
uint first;
}

[numthreads(1, 1, 1)]
void main(uint tid : SV_DispatchThreadID)
{
uint idx = first + tid;
uint val;
InterlockedAdd(RWBufs[idx][1], 200, val);
uint dummy_val;
// This will be masked, except for the first iteration, which has a large view.
InterlockedAdd(RWBufs[idx][2], 400, dummy_val);

uint size;
RWBufs[idx].GetDimensions(size);
uint ro_size;
Bufs[idx].GetDimensions(ro_size);

uint success = 0;

if (idx == 0)
{
    if (size == 16 * 1024 * 1024)
        success |= 0x10;
    if (ro_size == 16 * 1024 * 1024)
        success |= 0x20;
}
else
{
    if (size == 2)
        success |= 0x10;
    if (ro_size == 2)
        success |= 0x20;
}

if (idx < 1024)
{
    if (RWBufs[idx][0] == idx + 1)
        success |= 1;
    if (Bufs[idx][0] == idx + 1)
        success |= 4;
}
else
{
    if (RWBufs[idx][0] == 1)
        success |= 1;
    if (Bufs[idx][0] == 1)
        success |= 4;
}

if (idx == 0)
{
    if (RWBufs[idx][3] == 1)
        success |= 2;
    if (Bufs[idx][3] == 1)
        success |= 8;
}
else
{
    if (RWBufs[idx][3] == 0)
        success |= 2;
    if (Bufs[idx][3] == 0)
        success |= 8;
}

RWBufs[idx][0] = success;
}