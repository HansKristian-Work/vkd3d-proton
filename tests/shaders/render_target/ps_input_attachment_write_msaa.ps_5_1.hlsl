struct PSOut
{
	float color : SV_Target;
	float d : SV_Depth;
};

PSOut main(float4 pos : SV_Position, uint samp : SV_SampleIndex)
{
	PSOut psout;
	psout.color = floor(pos.x) + (float(samp) + 0.5) / 4.0;
	psout.d = (floor(pos.y) + (float(samp) + 0.5) / 4.0) / 256.0;
	return psout;
}
