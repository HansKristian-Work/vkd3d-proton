struct VSOut
{
	uint shading_rate : SV_ShadingRate;
	float4 position : SV_Position;
};

VSOut main(uint id : SV_VertexID)
{
	VSOut vout;
	vout.shading_rate = 0x5; // 2x2
	float2 coords = float2((id << 1) & 2, id & 2);
	vout.position = float4(coords * float2(2, -2) + float2(-1, 1), 0, 1);
	vout.position.y = -vout.position.y;
	return vout;
}
