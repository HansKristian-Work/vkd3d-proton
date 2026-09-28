float4 main(uint id : SV_VertexID) : SV_Position
{
	float4 pos;
	if (id == 0)
		pos = float4(-1, -1, 0, 1);
	else if (id == 1)
		pos = float4(-1, 3, 0, 1);
	else
		pos = float4(3, -1, 0, 1);

	return pos;
}
