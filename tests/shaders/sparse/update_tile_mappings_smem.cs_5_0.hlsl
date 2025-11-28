StructuredBuffer<uint> tiled_buffer : register(t0);
RWStructuredBuffer<uint> out_buffer : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 group_id : SV_GroupID)
{
    out_buffer[group_id.x] = tiled_buffer[16384 * group_id.x];
}
