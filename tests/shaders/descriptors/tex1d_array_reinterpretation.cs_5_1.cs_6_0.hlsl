Texture1D<float> T1D[] : register(t0, space0);
Texture1DArray<float> T1DArray[] : register(t0, space1);

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
	T1D[gid].GetDimensions(lid, out_info.dimensions.x, out_info.levels);
	T1DArray[gid].GetDimensions(lid, out_info.array_dimensions.x, out_info.array_layers, out_info.array_levels);

	out_info.sampled = round(255.0 * T1D[gid].Load(int2(0, lid)));
	out_info.array_sampled = round(255.0 * T1DArray[gid].Load(int3(0, lid % 4, lid / 4)));

	out_info.dimensions.y = 0;
	out_info.array_dimensions.y = 0;
	Outputs[thr] = out_info;
}
