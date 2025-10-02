cbuffer data_t : register(b0)
{
    uint4 in_data[12];
}

RWStructuredBuffer<uint2> out_data : register(u0);

[numthreads(12,1,1)]
void main(in uint3 tid : SV_DISPATCHTHREADID)
{
    if (tid.y == 0u)
      out_data[tid.x].y = in_data[tid.x].x;

    if (tid.x == 0u)
      out_data[tid.y].x = in_data[tid.y].x;
}
