struct VSIn
{
	float4 pos : SV_Position;
	uint layer : SV_RenderTargetArrayIndex;
	uint viewport : SV_ViewportArrayIndex;
};

struct VSOut
{
	float4 pos : SV_Position;
	uint2 color : COLOR;
};

[maxvertexcount(3)]
void main(triangle VSIn tri[3], inout TriangleStream<VSOut> vout)
{
	for (int i = 0; i < 3; i++)
	{
		VSOut v;
		v.pos = tri[i].pos;
		v.color = uint2(tri[i].layer, tri[i].viewport);
		vout.Append(v);
	}
}


