struct VSOut
{
	float4 pos : SV_Position;
	uint2 color : COLOR;
};

uint2 main(VSOut vout) : SV_Target
{
	return vout.color;
}

