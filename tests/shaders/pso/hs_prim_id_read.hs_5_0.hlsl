struct ControlPoint
{
	uint prim : PRIM;
	uint iid : IID;
};

struct Patch
{
    float tf_outer[3] : SV_TESSFACTOR;
    float tf_inner : SV_INSIDETESSFACTOR;
};

struct Input
{
	uint iid : IID;
};

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(1)]
[patchconstantfunc("main_patch")]
ControlPoint main(uint cid : SV_OutputControlPointID, InputPatch<Input, 1> input,
	uint prim : SV_PrimitiveID)
{
	ControlPoint cp;
	cp.prim = prim;
	cp.iid = input[0].iid;
	return cp;
}

Patch main_patch()
{
	Patch p;
	p.tf_outer[0] = 2.0;
	p.tf_outer[1] = 2.0;
	p.tf_outer[2] = 2.0;
	p.tf_inner = 2.0;
	return p;
}
