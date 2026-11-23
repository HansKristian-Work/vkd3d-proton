struct VSOut
{
	float4 pos : SV_Position;
	uint vp : SV_ViewportArrayIndex;
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VSOut vout;
	if (vid == 0)
		vout.pos = float4(-1, -1, 0, 1);
	else if (vid == 1)
		vout.pos = float4(3, -1, 0, 1);
	else
		vout.pos = float4(-1, 3, 0, 1);

	vout.vp = iid;
	return vout;
}
