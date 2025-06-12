float4 main(in uint sample_id : SV_SampleIndex) : SV_Target
{
	return float4(sample_id / 2, sample_id / 2, sample_id / 2, 1.0f);
}
