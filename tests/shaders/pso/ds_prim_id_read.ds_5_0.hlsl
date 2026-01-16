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

struct DSOut
{
	uint ds_prim : DS_PRIM;
	uint hs_prim : HS_PRIM;
	uint iid : IID;
};

[domain("tri")]
DSOut main(float3 coord : SV_DOMAINLOCATION, Patch patch, OutputPatch<ControlPoint, 1> cp, uint prim : SV_PrimitiveID)
{
	DSOut ds;
	ds.ds_prim = prim;
	ds.hs_prim = cp[0].prim;
	ds.iid = cp[0].iid;
	return ds;
}

