StructuredBuffer<uint> Indices : register(t0, space1);

static const uint ReferenceOutputOffset = 32 * 1024;
static const uint ReferenceOutputStride = 32;

struct Uint5 { uint4 a; uint b; };
struct Uint6 { uint3 a, b; };
struct Uint7 { uint4 a; uint3 b; };
struct Uint8 { uint4 a, b; };

RWStructuredBuffer<Uint8> Outputs : register(u32);

RWStructuredBuffer<uint> Outputs1[4] : register(u0);
RWStructuredBuffer<uint2> Outputs2[4] : register(u4);
RWStructuredBuffer<uint3> Outputs3[4] : register(u8);
RWStructuredBuffer<uint4> Outputs4[4] : register(u12);
RWStructuredBuffer<Uint5> Outputs5[4] : register(u16);
RWStructuredBuffer<Uint6> Outputs6[4] : register(u20);
RWStructuredBuffer<Uint7> Outputs7[4] : register(u24);
RWStructuredBuffer<Uint8> Outputs8[4] : register(u28);

StructuredBuffer<uint> Inputs1 : register(t0);
StructuredBuffer<uint2> Inputs2 : register(t1);
StructuredBuffer<uint3> Inputs3 : register(t2);
StructuredBuffer<uint4> Inputs4 : register(t3);
StructuredBuffer<Uint5> Inputs5 : register(t4);
StructuredBuffer<Uint6> Inputs6 : register(t5);
StructuredBuffer<Uint7> Inputs7 : register(t6);
StructuredBuffer<Uint8> Inputs8 : register(t7);

[numthreads(32, 1, 1)]
void main(uint thr : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
	uint index = Indices[thr];

	if (group_index < 4)
	{
		// Test load behavior.
		uint loaded = Inputs1[index];
		Outputs[thr].a.x = loaded;

		// Test store behavior for difficult indices.
		Outputs1[NonUniformResourceIndex(group_index - 0)][index] = thr + 1;
	}
	else if (group_index < 8)
	{
		uint2 loaded = Inputs2[index];
		Outputs[thr].a.xy = loaded;

		Outputs2[NonUniformResourceIndex(group_index - 4)][index] = 2 * thr + uint2(0, 1) + 1;
	}
	else if (group_index < 12)
	{
		uint3 loaded = Inputs3[index];
		Outputs[thr].a.xyz = loaded;

		Outputs3[NonUniformResourceIndex(group_index - 8)][index] = 3 * thr + uint3(0, 1, 2) + 1;
	}
	else if (group_index < 16)
	{
		uint4 loaded = Inputs4[index];
		Outputs[thr].a = loaded;

		Outputs4[NonUniformResourceIndex(group_index - 12)][index] = 4 * thr + uint4(0, 1, 2, 3) + 1;
	}
	else if (group_index < 20)
	{
		Uint5 loaded = Inputs5[index];
		Outputs[thr].a = loaded.a;
		Outputs[thr].b.x = loaded.b;

		loaded.a = 5 * thr + uint4(0, 1, 2, 3) + 1;
		loaded.b = 5 * thr + 4 + 1;
		Outputs5[NonUniformResourceIndex(group_index - 16)][index] = loaded;
	}
	else if (group_index < 24)
	{
		Uint6 loaded = Inputs6[index];
		Outputs[thr].a = uint4(loaded.a, loaded.b.x);
		Outputs[thr].b.xy = loaded.b.yz;

		loaded.a = 6 * thr + uint3(0, 1, 2) + 1;
		loaded.b = 6 * thr + uint3(3, 4, 5) + 1;
		Outputs6[NonUniformResourceIndex(group_index - 20)][index] = loaded;
	}
	else if (group_index < 28)
	{
		Uint7 loaded = Inputs7[index];
		Outputs[thr].a = loaded.a;
		Outputs[thr].b.xyz = loaded.b;

		loaded.a = 7 * thr + uint4(0, 1, 2, 3) + 1;
		loaded.b = 7 * thr + uint3(4, 5, 6) + 1;
		Outputs7[NonUniformResourceIndex(group_index - 24)][index] = loaded;
	}
	else
	{
		Uint8 loaded = Inputs8[index];
		Outputs[thr].a = loaded.a;
		Outputs[thr].b = loaded.b;

		loaded.a = 8 * thr + uint4(0, 1, 2, 3) + 1;
		loaded.b = 8 * thr + uint4(4, 5, 6, 7) + 1;
		Outputs8[NonUniformResourceIndex(group_index - 28)][index] = loaded;
	}
}
