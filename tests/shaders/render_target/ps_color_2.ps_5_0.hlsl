float4 color;

struct Outputs { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };

Outputs main(float4 position : SV_POSITION)
{
	Outputs outputs;
	outputs.c0 = color;
	outputs.c1 = color;
	return outputs;
}
