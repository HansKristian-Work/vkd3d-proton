struct PSOut
{
	float4 color : SV_Target;
};

Texture2D<float4> Tex;
SamplerState S;

void main(float4 pos : SV_Position, out PSOut psout)
{
	float4 v = Tex.Load(int3(pos.xy, 0));

	[branch]
	if (v.x && v.y && v.z && v.w)
	{
		psout.color = v;
		[branch]
		if (v.w > 0.1)
			discard;
	}
	else if (v.w == 10.0)
		psout.color = Tex.Load(int3(v.xy, 0)) + 2;
}
