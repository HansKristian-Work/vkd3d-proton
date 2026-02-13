cbuffer Cbuf : register(b0)
{
	uint color0;
	uint color1;
	uint mask;
	float d;
};

struct FragmentOut
{
	float4 color0 : SV_Target0;
	float4 color1 : SV_Target1;
	float d : SV_Depth;
	uint mask : SV_Coverage;
};

FragmentOut main()
{
	FragmentOut o;
	o.color0 = float4((color0.xxxx >> uint4(0, 8, 16, 24)) & 0xff) / 255.0;
	o.color1 = float4((color1.xxxx >> uint4(0, 8, 16, 24)) & 0xff) / 255.0;
	o.mask = mask;
	o.d = d;
	return o;
}
