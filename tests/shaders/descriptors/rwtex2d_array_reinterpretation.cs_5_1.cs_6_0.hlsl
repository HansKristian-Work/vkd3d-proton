RWTexture2D<float> T2D[] : register(u0, space1);
RWTexture2DArray<float> T2DArray[] : register(u0, space2);

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
	out_info.levels = 0;
	out_info.array_levels = 0;

	T2D[gid].GetDimensions(out_info.dimensions.x, out_info.dimensions.y);
	T2DArray[gid].GetDimensions(out_info.array_dimensions.x, out_info.array_dimensions.y, out_info.array_layers);

	out_info.sampled = round(255.0 * T2D[gid].Load(int2(0, 0)));
	out_info.array_sampled = round(255.0 * T2DArray[gid].Load(int3(0, 0, lid)));

	Outputs[thr] = out_info;
}
