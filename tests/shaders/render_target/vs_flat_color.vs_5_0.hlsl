float4 main(uint vid : SV_VertexID) : SV_Position
{
	float4 pos;
	pos.x = float(vid & 1) * 4.0 - 1.0;
	pos.y = float(vid & 2) * 2.0 - 1.0;
	pos.z = 0.0;
	pos.w = 1.0;
	return pos;
}
