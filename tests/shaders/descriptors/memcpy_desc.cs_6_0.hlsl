StructuredBuffer<uint> Inputs[] : register(t0);
RWStructuredBuffer<uint> Outputs[] : register(u0);

cbuffer buf { uint index; };

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Outputs[index][thr] = Inputs[index][thr];
}
