RWStructuredBuffer<uint> Buffers[] : register(u2, space1);

[numthreads(64, 1, 1)]
void main(uint global_index : SV_DispatchThreadID, uint index : SV_GroupID)
{
    // Need branch here or FXC complains about race condition.
    if (global_index < 512)
    {
        Buffers[NonUniformResourceIndex(global_index)][0] = global_index + 1;
        Buffers[NonUniformResourceIndex(global_index & ~3)].IncrementCounter();
    }
}