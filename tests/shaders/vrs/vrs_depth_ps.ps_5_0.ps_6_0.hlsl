struct VSOut
{
	float depth : D;
	float4 pos : SV_Position;
};

struct POut
{
	uint col : SV_Target;
	float depth : SV_Depth;
};

POut main(VSOut vin)
{
	POut p;
	const float epsilon = 1.0 / 256.0;
	p.col = uint(vin.depth * 64.0 + epsilon); // To avoid micro-FP precision issues in depth interpolation.
	p.depth = vin.depth;
	return p;
}
