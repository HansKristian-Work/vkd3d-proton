struct VOut
{
	float4 pos : SV_Position;
	nointerpolation float i : I;
};

struct Cbuf
{
	uint value;
};

ConstantBuffer<Cbuf> Bufs[] : register(b0);

float main(VOut vin) : SV_Target
{
	// Drop nonuniform here. AMD D3D12 makes this magically work, and at least one game relies on this behavior.
	return float(Bufs[uint(vin.i)].value);
}

