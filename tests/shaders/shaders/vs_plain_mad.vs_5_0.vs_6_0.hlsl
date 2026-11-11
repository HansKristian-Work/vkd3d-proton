StructuredBuffer<float3> Inputs : register(t0);

struct VSOut
{
	float4 pos : SV_Position;
	nointerpolation float v : V;
};

VSOut main(uint vid : SV_VertexID)
{
	VSOut vout;
	if (vid == 0)
		vout.pos = float4(-1, -1, 0, 1);
	else if (vid == 1)
		vout.pos = float4(3, -1, 0, 1);
	else
		vout.pos = float4(-1, 3, 0, 1);

	float3 v = Inputs[vid];
	float res = mad(v.x, v.y, v.z);
	vout.v = res;

	return vout;
}
