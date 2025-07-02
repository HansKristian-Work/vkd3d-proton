struct VOut
{
	float2 uv : UV;
	float4 pos : SV_Position;
};

VOut main(uint vid : SV_VertexID)
{
	VOut vout;
	if (vid == 0)
		vout.pos = float4(-1, -1, 0, 1);
	else if (vid == 1)
		vout.pos = float4(+3, -1, 0, 1);
	else
		vout.pos = float4(-1, +3, 0, 1);

	vout.uv = vout.pos.xy * float2(10, 15);
	return vout;
}
