struct VSOut
{
	float4 pos : SV_Position;
	uint layer : SV_RenderTargetArrayIndex;
	uint viewport : SV_ViewportArrayIndex;
};

struct HSPatchOut
{
	float edges[3] : SV_TessFactor;
	float inside[1] : SV_InsideTessFactor;
};

[domain("tri")]
VSOut main(OutputPatch<VSOut, 3> patch, float3 coord : SV_DomainLocation, HSPatchOut pc)
{
	VSOut vout;
	vout.pos = patch[0].pos * coord.x + patch[1].pos * coord.y + patch[2].pos * coord.z;
	vout.layer = patch[0].layer;
	vout.viewport = patch[1].viewport;
	return vout;
}
