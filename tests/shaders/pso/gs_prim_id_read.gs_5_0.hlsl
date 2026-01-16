struct DSOut
{
	uint ds_prim : DS_PRIM;
	uint hs_prim : HS_PRIM;
	uint iid : IID;
};

struct GSOut
{
	uint4 prim : PRIM;
};

[maxvertexcount(1)]
void main(triangle DSOut vertices[3],
	inout PointStream<GSOut> out_stream,
	uint prim : SV_PrimitiveID)
{
	GSOut gout;

	gout.prim.x = vertices[0].ds_prim;
	gout.prim.y = vertices[0].hs_prim;
	gout.prim.z = vertices[0].iid;
	gout.prim.w = prim;
	out_stream.Append(gout);
}
