struct VOut
{
	float4 col : COLOR;
	float3 pos : POSITION;
};

cbuffer Constants : register(b0)
{
	float4 color;
};

VOut main(uint vid : SV_VertexID)
{
	VOut vout;
	vout.col = color;
	vout.pos.x = float(vid & 1);
	vout.pos.y = float(vid & 2) * 0.5;
	vout.pos.z = 0.0;

	vout.pos.xy *= 2.0;
	vout.pos.xy -= 1.0;

	return vout;
}
