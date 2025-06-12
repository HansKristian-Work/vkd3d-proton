cbuffer Buf : register(b0) { float depth; float iid_delta; };

struct VOut { float4 pos : SV_Position; uint iid : IID; };

VOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VOut vout;
	vout.pos.x = 4.0 * float(vid & 1) - 1.0;
	vout.pos.y = 2.0 * float(vid & 2) - 1.0;
	vout.pos.z = depth + iid_delta * float(iid);
	vout.pos.w = 1.0;
	vout.iid = iid;
	return vout;
}
