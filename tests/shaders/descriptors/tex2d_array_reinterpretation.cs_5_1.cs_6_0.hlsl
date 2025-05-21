Texture2D<float> T2D[] : register(t0, space0);
Texture2DArray<float> T2DArray[] : register(t0, space1);

struct OutputInfo
{
	uint2 dimensions;
	uint2 array_dimensions;
	uint array_layers;
	uint levels;
	uint array_levels;
	float sampled;
	float array_sampled;
};

RWStructuredBuffer<OutputInfo> Outputs : register(u0);

[numthreads(16, 1, 1)]
void main(uint thr : SV_DispatchThreadID, uint gid : SV_GroupID, uint lid : SV_GroupIndex)
{
	OutputInfo out_info;
	T2D[gid].GetDimensions(lid, out_info.dimensions.x, out_info.dimensions.y, out_info.levels);
	T2DArray[gid].GetDimensions(lid, out_info.array_dimensions.x, out_info.array_dimensions.y, out_info.array_layers, out_info.array_levels);

	out_info.sampled = round(255.0 * T2D[gid].Load(int3(0, 0, lid)));
	out_info.array_sampled = round(255.0 * T2DArray[gid].Load(int4(0, 0, lid % 4, lid / 4)));

	Outputs[thr] = out_info;
}
