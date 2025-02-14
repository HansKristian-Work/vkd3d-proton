struct VOut
{
	nointerpolation uint a : A;
	nointerpolation float b : B;
	float4 pos : SV_Position;
};

cbuffer cbuf : register(b0)
{
	uint a;
	uint b;
};

VOut main(uint vid : SV_VertexID)
{
	VOut vout;
	vout.a = a + vid;
	vout.b = b + vid;
	if (vid == 0)
		vout.pos = float4(-1.0, -1.0, 0.0, 1.0);
	else if (vid == 1)
		vout.pos = float4(-1.0, +3.0, 0.0, 1.0);
	else
		vout.pos = float4(+3.0, +1.0, 0.0, 1.0);
	return vout;
}
