struct VSOut
{
	float4 positions[4] : FROG;
	uint prim : PRIM;
};

VSOut main(float4 in_position : POSITION, uint prim : SV_InstanceID)
{
	VSOut vout;
	vout.positions[0] = 0.5 * in_position + float4(-0.5, -0.5, 0, 0);
	vout.positions[1] = 0.5 * in_position + float4(+0.5, -0.5, 0, 0);
	vout.positions[2] = 0.5 * in_position + float4(-0.5, +0.5, 0, 0);
	vout.positions[3] = 0.5 * in_position + float4(+0.5, +0.5, 0, 0);
	vout.prim = prim;
	return vout;
}
