RWByteAddressBuffer Outputs[] : register(u0);
ByteAddressBuffer Inputs : register(t0);
StructuredBuffer<uint> Indices : register(t0, space1);

static const uint ReferenceOutputOffset = 32 * 1024;
static const uint ReferenceOutputStride = 32;

struct Uint5 { uint4 a; uint b; };
struct Uint6 { uint3 a, b; };
struct Uint7 { uint4 a; uint3 b; };
struct Uint8 { uint4 a, b; };

[numthreads(32, 1, 1)]
void main(uint thr : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
	uint index = Indices[thr];

	if (group_index < 4)
	{
		// Test load behavior.
		uint loaded = Inputs.Load(4 * index); 
		Outputs[32].Store(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		// Test store behavior for difficult indices.
		Outputs[NonUniformResourceIndex(group_index)].Store(4 * index, thr + 1);
	}
	else if (group_index < 8)
	{
		uint2 loaded = Inputs.Load2(8 * index); 
		Outputs[32].Store2(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		Outputs[NonUniformResourceIndex(group_index)].Store2(8 * index, 2 * thr + uint2(0, 1) + 1);
	}
	else if (group_index < 12)
	{
		uint3 loaded = Inputs.Load3(12 * index); 
		Outputs[32].Store3(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		Outputs[NonUniformResourceIndex(group_index)].Store3(12 * index, 3 * thr + uint3(0, 1, 2) + 1);
	}
	else if (group_index < 16)
	{
		uint4 loaded = Inputs.Load4(16 * index); 
		Outputs[32].Store4(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		Outputs[NonUniformResourceIndex(group_index)].Store4(16 * index, 4 * thr + uint4(0, 1, 2, 3) + 1);
	}
	else if (group_index < 20)
	{
		Uint5 loaded = Inputs.Load<Uint5>(20 * index); 
		Outputs[32].Store<Uint5>(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		loaded.a = 5 * thr + uint4(0, 1, 2, 3) + 1;
		loaded.b = 5 * thr + 4 + 1;
		Outputs[NonUniformResourceIndex(group_index)].Store<Uint5>(20 * index, loaded);
	}
	else if (group_index < 24)
	{
		Uint6 loaded = Inputs.Load<Uint6>(24 * index); 
		Outputs[32].Store<Uint6>(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		loaded.a = 6 * thr + uint3(0, 1, 2) + 1;
		loaded.b = 6 * thr + uint3(3, 4, 5) + 1;
		Outputs[NonUniformResourceIndex(group_index)].Store<Uint6>(24 * index, loaded);
	}
	else if (group_index < 28)
	{
		Uint7 loaded = Inputs.Load<Uint7>(28 * index); 
		Outputs[32].Store<Uint7>(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		loaded.a = 7 * thr + uint4(0, 1, 2, 3) + 1;
		loaded.b = 7 * thr + uint3(4, 5, 6) + 1;
		Outputs[NonUniformResourceIndex(group_index)].Store<Uint7>(28 * index, loaded);
	}
	else
	{
		Uint8 loaded = Inputs.Load<Uint8>(32 * index); 
		Outputs[32].Store<Uint8>(ReferenceOutputOffset + ReferenceOutputStride * thr, loaded);

		loaded.a = 8 * thr + uint4(0, 1, 2, 3) + 1;
		loaded.b = 8 * thr + uint4(4, 5, 6, 7) + 1;
		Outputs[NonUniformResourceIndex(group_index)].Store<Uint8>(32 * index, loaded);
	}
}
