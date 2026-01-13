struct PSOut
{
	uint rt : SV_Target;
	float d : SV_Depth;
};

Texture2D<uint> RT : register(t100, space1);
Texture2D<float> Depth : register(t101, space1);

PSOut main(float4 pos : SV_Position)
{
	PSOut pout;

	pout.rt = RT.Load(int3(pos.xy, 0));
	pout.d = Depth.Load(int3(pos.xy, 0));

	pout.rt += uint(pout.d * 1024.0);
	pout.d += pos.x / 1024.0 + pos.y / 512.0;

	return pout;
}
