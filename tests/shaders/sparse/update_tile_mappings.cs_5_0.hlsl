StructuredBuffer<uint> tiled_buffer : register(t0);
RWStructuredBuffer<uint> out_buffer : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    out_buffer[thread_id.x] = tiled_buffer[16384 * thread_id.x];
}