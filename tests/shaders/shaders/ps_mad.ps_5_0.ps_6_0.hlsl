struct VSOut
{
	float4 pos : SV_Position;
	nointerpolation float v : V;
};

float main(VSOut vout) : SV_Target
{
	return vout.v;
}

