struct LongVecStruct
{
	vector<float, 6> V6Arr[2]; // Array of vector is allowed.
	vector<float, 5> V5; // Vector member is allowed.
};

// Test heap desc path.
RWStructuredBuffer<LongVecStruct> RWStruct : register(u0);
RWByteAddressBuffer RWBAB : register(u1);

// Test root desc path.
RWStructuredBuffer<LongVecStruct> RWStructBDA : register(u2);
RWByteAddressBuffer RWBABBDA : register(u3);

groupshared LongVecStruct LDS[16];
static LongVecStruct Private;

[numthreads(4, 1, 1)]
void main(uint thr : SV_DispatchThreadID, uint local_index : SV_GroupIndex)
{
	// Test basic loads.
	LongVecStruct loaded0 = RWStruct[thr];
	LongVecStruct loaded1 = RWBAB.Load<LongVecStruct>(thr * sizeof(LongVecStruct));
	LongVecStruct loaded2 = RWStructBDA[thr];
	LongVecStruct loaded3 = RWBABBDA.Load<LongVecStruct>(thr * sizeof(LongVecStruct));

	// Constant index insertion and extraction. Swizzles are not supported.
	loaded0.V5[1] = loaded0.V6Arr[0][2];

	Private = loaded0;

	// Test dynamic insertion and extraction.
	Private.V5[local_index] = Private.V5[local_index ^ 1] + 1.0;
	Private.V6Arr[local_index & 1] = Private.V6Arr[(local_index & 1) ^ 1];

	// Test basic stores.
	LongVecStruct Func = Private;

	// Test dynamic insertion and extraction on function local variables.
	Func.V5[local_index] = Func.V5[local_index ^ 1] + 4.0;
	Func.V6Arr[local_index & 1] = Func.V6Arr[(local_index & 1) ^ 1];

	loaded0 = Func;

	// Shuffle through LDS.
	LDS[4 * local_index + 0] = loaded0;
	LDS[4 * local_index + 1] = loaded1;
	LDS[4 * local_index + 2] = loaded2;
	LDS[4 * local_index + 3] = loaded3;

	GroupMemoryBarrierWithGroupSync();

	RWStruct[thr] = Func;

	Func = LDS[4 * (local_index ^ 1) + 1];
	RWBAB.Store<LongVecStruct>(thr * sizeof(Func), Func);
	Func = LDS[4 * (local_index ^ 1) + 2];
	RWStructBDA[thr] = Func;
	Func = LDS[4 * (local_index ^ 1) + 3];
	RWBABBDA.Store<LongVecStruct>(thr * sizeof(Func), Func);
}
