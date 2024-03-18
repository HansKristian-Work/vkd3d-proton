Texture2D<uint> tiled_texture : register(t0);
RWStructuredBuffer<uint> out_buffer : register(u0);

[numthreads(28,1,1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 tile_size = uint2(128, 128);
    uint tile_index = 0;
    uint tile_count = 4;
    uint mip_count = 10;
    uint mip_level = 0;

    while (thread_id.x >= tile_index + tile_count * tile_count && mip_level < mip_count)
    {
        tile_index += tile_count * tile_count;
        tile_count = max(tile_count / 2, 1);
        mip_level += 1;
    }

    uint2 tile_coord;
    tile_coord.x = (thread_id.x - tile_index) % tile_count;
    tile_coord.y = (thread_id.x - tile_index) / tile_count;

    out_buffer[thread_id.x] = tiled_texture.mips[mip_level][tile_coord * tile_size];
}