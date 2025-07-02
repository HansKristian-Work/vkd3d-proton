cbuffer Cbuf : register(b0)
{
	float4 quad_values;
	float4 values[4];
};

struct VOut
{
	float2 uv : UV;
	float4 pos : SV_Position;
};

RWStructuredBuffer<float4> RWBuf : register(u0);

void main(VOut vin)
{
	// Try to make sure we test OpVariable condition for hoisting.
	float tmp[4];
	tmp[0] = quad_values.x;
	tmp[1] = quad_values.y;
	tmp[2] = quad_values.z;
	tmp[3] = quad_values.w;

	int coord = (int(vin.pos.x) & 1) + (int(vin.pos.y) & 1) * 2;
	
	if (tmp[coord] < 0.0)
		return;

	// Ensure that masked lanes don't write.
	RWBuf[coord] = float4(1, 1, 1, 1);

	// We're now in varying control flow.
	// We can still perform derivatives based on shader inputs, also, statically indexed constants (becoming 0 effectively).
	float x = ddx_fine(vin.uv.x);
	float y = ddy_fine(vin.uv.y);
	float z = ddx_coarse(values[0].x);
	float w = ddy_coarse(values[0].y);

	RWBuf[coord] += float4(x, y, z, w);
}
