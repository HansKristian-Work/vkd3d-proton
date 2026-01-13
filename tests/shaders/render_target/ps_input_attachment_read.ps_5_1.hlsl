struct PSOut
{
	uint4 rt0 : SV_Target0;
	float4 rt1 : SV_Target1;
	uint4 rt2 : SV_Target2;
	float4 rt3 : SV_Target3;
};

Texture2D<uint4> RT4 : register(t104, space1);
Texture2D<float4> RT5 : register(t105, space1);
Texture2D<uint4> RT6 : register(t106, space1);
Texture2D<float4> RT7 : register(t107, space1);
Texture2D<float> Depth : register(t108, space1);
Texture2D<uint> Stencil : register(t109, space1);

PSOut main(float4 pos : SV_Position)
{
	PSOut pout;

	uint4 rt4 = RT4.Load(int3(pos.xy, 0));
	float4 rt5 = RT5.Load(int3(pos.xy, 0));
	uint4 rt6 = RT6.Load(int3(pos.xy, 0));
	float4 rt7 = RT7.Load(int3(pos.xy, 0));
	float depth = Depth.Load(int3(pos.xy, 0));
	uint stencil = Stencil.Load(int3(pos.xy, 0));

	pout.rt0 = rt4 + 1;
	pout.rt1 = rt5 + 1.0 / 255.0;
	pout.rt2 = rt6 + 1;
	pout.rt3 = rt7 + 1.0 / 255.0;

	pout.rt0.x += uint(100.0 * depth);
	pout.rt0.y += stencil;

	return pout;
}
