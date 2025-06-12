RWStructuredBuffer<uint> Buffers[] : register(u2, space1);
RWTexture2D<uint> Textures[] : register(u2, space2);

[numthreads(64, 1, 1)]
void main(uint global_index : SV_DispatchThreadID, uint index : SV_GroupID)
{
    // Need this branch or FXC refuses to compile. It doesn't understand we're writing to different resources.
    if (global_index < 512)
    {
        Buffers[NonUniformResourceIndex(global_index)][0] = global_index + 1;
        Textures[NonUniformResourceIndex(global_index)][int2(0, 0)] = 256 + global_index + 1;
    }
}