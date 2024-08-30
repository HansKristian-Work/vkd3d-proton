struct VIn
{
	uint i : I;
	uint vid : SV_VertexID;
};

cbuffer Cbuf : register(b0) { float4 v; };

struct VOut
{
	float4 c : C;
	float4 pos : SV_Position;
};

VOut main(VIn vin)
{
	VOut vout;
	vout.pos.x = 4.0 * float(vin.vid & 1) - 1.0;
	vout.pos.y = 2.0 * float(vin.vid & 2) - 1.0;
	vout.pos.z = 0.0;
	vout.pos.w = 1.0;
	vout.c = v;
	vout.c.x += float(vin.i);
	return vout;
}
