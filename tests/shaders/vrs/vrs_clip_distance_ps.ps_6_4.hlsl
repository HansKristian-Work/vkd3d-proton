struct VSOut
{
	uint shading_rate : SV_ShadingRate;
	float clip : SV_ClipDistance;
	float attr : ATTR;
	float4 position : SV_Position;
};

float4 main(VSOut vin) : SV_Target
{
	float4 res;
	res.x = vin.clip;
	res.yz = vin.position.xy;
	res.w = vin.attr;

	return res;
}
