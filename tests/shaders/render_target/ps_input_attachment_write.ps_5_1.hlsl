struct PSOut
{
	uint4 rt4 : SV_Target4;
	float4 rt5 : SV_Target5;
	uint4 rt6 : SV_Target6;
	float4 rt7 : SV_Target7;
	float d : SV_Depth;
};

PSOut main()
{
	PSOut pout;
	pout.rt4 = uint4(1, 2, 3, 4);
	pout.rt5 = float4(5, 6, 7, 8) / 255.0;
	pout.rt6 = uint4(9, 10, 11, 12);
	pout.rt7 = float4(13, 14, 15, 16) / 255.0;
	pout.d = 0.25;

	return pout;
}
