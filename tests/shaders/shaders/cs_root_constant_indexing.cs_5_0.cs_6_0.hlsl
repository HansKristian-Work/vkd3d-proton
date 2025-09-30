cbuffer data_t : register(b0)
{
    uint4 in_data[12];
}

RWStructuredBuffer<uint> out_data : register(u0);

[numthreads(1,1,1)]
void main(in uint3 tid : SV_GROUPID)
{
    out_data[tid.x] = in_data[tid.x].x;
}
