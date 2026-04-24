struct VOut
{
	float4 pos : SV_Position;
	float cookie : COOKIE;
	uint vid : VID;
	uint iid : IID;
};

cbuffer buf : register(b0)
{
	float cookie;
};

VOut main(
	uint id : SV_VertexID,
	uint iid : SV_InstanceID,
	uint vid : SV_ViewID)
{
	bool vid_xor = cookie > 200.0 ? 1 : 0;
	VOut vout;
	vout.pos.x = float(id & 1) - 1.0;
	vout.pos.y = float(id & 2) * 0.5 - 1.0;
	vout.pos.x += float(iid);
	vout.pos.y += float(vid ^ vid_xor);
	vout.pos.z = float(iid + 2 * vid) / 4.0;
	vout.pos.w = 1.0;
    vout.vid = vid;
    vout.iid = iid;
	vout.cookie = cookie;
	vout.pos.y = -vout.pos.y;
	return vout;
}
