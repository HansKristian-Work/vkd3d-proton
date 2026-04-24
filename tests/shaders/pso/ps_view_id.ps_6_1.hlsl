struct VOut
{
	float4 pos : SV_Position;
	float cookie : COOKIE;
	uint vid : VID;
	uint iid : IID;
};

float4 main(VOut vin) : SV_Target
{
	return float4(vin.cookie, vin.vid, vin.iid, 0);
}
