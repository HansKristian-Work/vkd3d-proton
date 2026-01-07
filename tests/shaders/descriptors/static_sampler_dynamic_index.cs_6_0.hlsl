Texture2D<float> T : register(t0);
SamplerState S[] : register(s0, space100);
RWStructuredBuffer<float> Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint gid : SV_GroupID)
{
	Output[gid] = T.SampleLevel(S[gid], 0.5.xx, 0.0);
}
