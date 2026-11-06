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

HSPatchOut patch_main(OutputPatch<VSOut, 3> cp)
{
	HSPatchOut o;
	int i;
	for (i = 0; i < 3; i++)
		o.edges[i] = 1.0;
	for (i = 0; i < 1; i++)
		o.inside[i] = 1.0;
	return o;
}

[domain("tri")]
[outputcontrolpoints(3)]
[partitioning("integer")]
[outputtopology("triangle_ccw")]
[patchconstantfunc("patch_main")]
VSOut main(InputPatch<VSOut, 3> input, uint i : SV_OutputControlPointID)
{
	return input[i];
}

