struct VOut
{
	float4 col : COLOR;
};

float4 main(VOut vout) : SV_Target
{
	return vout.col;
}
