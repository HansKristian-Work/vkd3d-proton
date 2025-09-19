RWStructuredBuffer<uint> RWUint1[4] : register(u0);
RWStructuredBuffer<uint2> RWUint2[4] : register(u4);
RWStructuredBuffer<uint3> RWUint3[4] : register(u8);
RWStructuredBuffer<uint4> RWUint4[4] : register(u12);

RWByteAddressBuffer BAB[16] : register(u16);

[numthreads(1, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
	if (idx == 0)
	{
		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				RWUint1[j][i] = i;

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				RWUint2[j][i] = 2 * i + uint2(0, 1);

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				RWUint3[j][i] = 3 * i + uint3(0, 1, 2);

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				RWUint4[j][i] = 4 * i + uint4(0, 1, 2, 3);

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				BAB[4 * 0 + j].Store(4 * i + 4 * j, 4 * j + i);

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				BAB[4 * 1 + j].Store2(4 * i + 4 * j, 2 * (4 * j + i) + uint2(0, 1));

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				BAB[4 * 2 + j].Store3(4 * i + 4 * j, 3 * (4 * j + i) + uint3(0, 1, 2));

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
				BAB[4 * 3 + j].Store4(4 * i + 4 * j, 4 * (4 * j + i) + uint4(0, 1, 2, 3));
	}
}

