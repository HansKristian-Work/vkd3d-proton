struct PSOut
{
	float color : SV_Target;
	float d : SV_Depth;
};

Texture2DMS<float> RT : register(t100, space1);
Texture2DMS<float> Depth : register(t101, space1);
RWTexture2D<float4> Output : register(u0);

void main(float4 pos : SV_Position)
{
	float2 lo = 1000.0.xx;
	float2 hi = 0.0.xx;

	for (int i = 0; i < 4; i++)
	{
		float rt = RT.Load(int2(pos.xy), i);
		float d = Depth.Load(int2(pos.xy), i);

		lo = min(lo, float2(rt, d));
		hi = max(hi, float2(rt, d));
	}

	Output[int2(pos.xy)] = float4(lo, hi);
}
