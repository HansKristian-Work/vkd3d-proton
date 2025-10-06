RWStructuredBuffer<uint> Output : register(u0);
StructuredBuffer<uint> InputsU32 : register(t0);
StructuredBuffer<uint16_t> InputsU16 : register(t1);
ByteAddressBuffer InputsBAB : register(t2);
ByteAddressBuffer InputsBAB_alt : register(t3);

cbuffer Offsets : register(b0)
{
	uint o0, o1, o2, o3;
};

[numthreads(64, 1, 1)]
void main(uint thr : SV_GroupIndex)
{
	Output[64 * 0 + thr] = InputsU32[thr + o0];
	Output[64 * 1 + thr] = InputsU16[2 * thr + o1];
	Output[64 * 2 + thr] = InputsU16[2 * thr + o2];
	Output[64 * 3 + thr] = InputsBAB.Load<uint>(4 * thr + o3);
	Output[64 * 4 + thr] = InputsBAB_alt.Load<uint16_t>(4 * thr + o3);
}
