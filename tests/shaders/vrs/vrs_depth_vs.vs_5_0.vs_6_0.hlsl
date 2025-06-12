struct VSOut
{
	float depth : D;
	float4 pos : SV_Position;
};

VSOut main(uint vid : SV_VertexID)
{
	VSOut vout;
	vout.pos = float4(float(vid & 1) * 4.0 - 1.0, float(vid & 2) * 2.0 - 1.0, 0.0, 1.0);
	vout.depth = vout.pos.x * 0.5 + 0.5;
	vout.pos.z = vout.depth;
	return vout;
}
