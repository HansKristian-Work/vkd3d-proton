StructuredBuffer<float> V : register(t0);

[WaveOpsIncludeHelperLanes]
float4 main(float4 pos : SV_Position) : SV_Target
{
	int pack = WaveActiveCountBits(true);
	int idx = (int(pos.x) & 1) + (int(pos.y) & 1) * 2;
	float value = V[idx];

	if (value > 0.5)
		discard;

	[branch]
	if (value >= 0.0)
	{
		// Only helpers enter this path.
		[branch]
		if (WaveActiveAllTrue(value > 1.0))
		{
			value += WaveReadLaneFirst(value);
			value += V[idx + 4];
		}
	}

	float4 o;
	o.x = value;
	o.y = QuadReadAcrossX(value);
	o.z = QuadReadAcrossY(value);
	o.w = QuadReadAcrossDiagonal(value);
	return o;
}
