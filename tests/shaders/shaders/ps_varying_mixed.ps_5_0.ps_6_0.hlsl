struct VOut
{
	nointerpolation uint a : A;
	nointerpolation float b : B;
	float4 pos : SV_Position;
};

float2 main(VOut vin) : SV_Target
{
	return float2(float(vin.a), vin.b);
}
