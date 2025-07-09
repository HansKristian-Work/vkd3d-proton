RWStructuredBuffer<uint2> Outputs : register(u0);
StructuredBuffer<uint> HeapIndices : register(t0);

StructuredBuffer<uint> HeapBuffers[] : register(t0, space1);

[numthreads(8, 1, 1)]
void main(uint gid : SV_GroupID, uint tid : SV_DispatchThreadID)
{
	uint nuri_index = HeapIndices[tid & 31];
	uint group_index = HeapIndices[gid];
	Outputs[tid] = uint2(HeapBuffers[NonUniformResourceIndex(nuri_index)][0], HeapBuffers[group_index][0]);
}
