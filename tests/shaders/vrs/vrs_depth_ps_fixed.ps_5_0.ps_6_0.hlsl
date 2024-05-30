struct VSOut
{
	float depth : D;
	float4 pos : SV_Position;
};

struct POut
{
	uint col : SV_Target;
};

POut main(VSOut vin)
{
	POut p;
	const float epsilon = 1.0 / 256.0;
	p.col = uint(vin.depth * 64.0 + epsilon); // To avoid micro-FP precision issues in depth interpolation.
	return p;
}
