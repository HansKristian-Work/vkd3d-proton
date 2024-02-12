struct VIn
{
	float4 col : COLOR;
	float3 pos : POSITION;
};

struct VOut
{
	float4 col : COLOR;
	float4 pos : SV_Position;
	uint layer : SV_RenderTargetArrayIndex;
};

cbuffer Transforms : register(b0)
{
	float4 dummy;
	float4x4 matrices[6];
};

[maxvertexcount(18)]
void main(triangle VIn inp[3], inout TriangleStream<VOut> outp)
{
	VOut vout;
	for (uint i = 0; i < 6; i++)
	{
		for (uint v = 0; v < 3; v++)
		{
			vout.pos = mul(float4(inp[v].pos, 1.0), matrices[i]);
			vout.col = inp[v].col;
			vout.layer = i;
			outp.Append(vout);
		}
		outp.RestartStrip();
	}
}

