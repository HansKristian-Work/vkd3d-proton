struct VOut
{
	float4 pos : SV_Position;
	nointerpolation float i : I;
};

VOut main(uint vid : SV_VertexID)
{
	VOut vout;

	uint quad_index = vid / 4;
	uint quad_x = quad_index % 8;
	uint quad_y = quad_index / 8;
	uint inner_x = vid & 1;
	uint inner_y = (vid & 2) >> 1;

	float2 coord;
	coord.x = float(quad_x + inner_x);
	coord.y = float(quad_y + inner_y);
	// Normalize to [0, 1]
	coord /= 8.0;
	// Clip space
	coord = 2.0 * coord - 1.0;
	// Y flip
	coord.y = -coord.y;

	vout.i = uint(quad_index);
	vout.pos = float4(coord, 0.0, 1.0);

	return vout;
}
