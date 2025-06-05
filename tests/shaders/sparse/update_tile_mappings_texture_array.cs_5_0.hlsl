Texture2DArray<uint> tiled_texture : register(t0);
RWStructuredBuffer<uint> out_buffer : register(u0);

[numthreads(36,1,1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    uint mip_level = thread_id.x % 9u;
    uint array_layer = thread_id.x / 9u;

    out_buffer[thread_id.x] = tiled_texture.mips[mip_level][uint3(0u, 0u, array_layer)];
}
