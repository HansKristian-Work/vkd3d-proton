RWStructuredBuffer<uint> RW : register(u1);

float main(float4 pos : SV_Position, uint samp : SV_SampleIndex, uint cov : SV_Coverage) : SV_Target
{
	bool is_helper = IsHelperLane();

	bool4 helper;
	helper.x = QuadReadLaneAt(is_helper, 0);
	helper.y = QuadReadLaneAt(is_helper, 1);
	helper.z = QuadReadLaneAt(is_helper, 2);
	helper.w = QuadReadLaneAt(is_helper, 3);

	cov |= uint(helper.x) << 28;
	cov |= uint(helper.y) << 29;
	cov |= uint(helper.z) << 30;
	cov |= uint(helper.w) << 31;

	RW[4 * samp + 2 * int(pos.y) + int(pos.x)] = cov;
	return float(samp);
}
