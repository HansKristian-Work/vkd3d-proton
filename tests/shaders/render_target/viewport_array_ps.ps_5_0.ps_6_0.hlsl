struct VSOut
{
	float4 pos : SV_Position;
	uint vp : SV_ViewportArrayIndex;
};

cbuffer cbuf : register(b0)
{
	uint p;
};

float4 main(VSOut vout) : SV_Target
{
	float4 c = 0.0.xxxx;
	c.x = 1.0 / 255.0;
	c.y = float(vout.vp) / 255.0;
	c.z = float(p) / 255.0;
	return c;
}
