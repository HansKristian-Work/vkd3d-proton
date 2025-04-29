float decode_srgb(float v)
{
	v = saturate(v);
	if (v < 0.0404482362)
		return v / 12.92;
	else
		return pow((v + 0.055) / 1.055, 2.4);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	float x = (pos.x - 0.5) / 255.0;
	float y = (pos.y - 0.5) / 255.0;
	return float4(decode_srgb(x), decode_srgb(y), 0, 0);
}
