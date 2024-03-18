cbuffer buf : register(b0) { float col; float iid_delta; };

struct VOut { float4 pos : SV_Position; uint iid : IID; };

RWStructuredBuffer<uint> Counter : register(u1);

float main(VOut vin) : SV_Target
{
	uint o;
	InterlockedAdd(Counter[0], 1, o);
	return col + float(vin.iid) * iid_delta;
}
