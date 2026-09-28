Texture2DMS<uint> Tex;

uint4 main(float4 pos : SV_Position) : SV_Target
{
	uint4 ret;
	ret.x = Tex.Load(int2(pos.xy), 0);
	ret.y = Tex.Load(int2(pos.xy), 1);
	ret.z = Tex.Load(int2(pos.xy), 2);
	ret.w = Tex.Load(int2(pos.xy), 3);
	return ret;
}
