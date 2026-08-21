struct [raypayload] Payload
{
	uint value : read(miss, caller) : write(miss, caller);
};

struct Results
{
	uint data[32];
};

struct Attr
{
	uint2 attr;
};

RWStructuredBuffer<Results> Buf : register(u0);

[shader("miss")]
void RayMiss0(inout Payload payload)
{
	payload.value += 17;
}

[shader("miss")]
void RayMiss1(inout Payload payload)
{
	payload.value += 13;
}

bool check_identity(float3x4 m)
{
	bool isidentity = true;
	for (int i = 0; i < 3; i++)
		isidentity = isidentity & all(m[i] == float4(i == 0, i == 1, i == 2, 0));
	return isidentity;
}

bool check_identity(float4x3 m)
{
	bool isidentity = true;
	for (int i = 0; i < 4; i++)
		isidentity = isidentity & all(m[i] == float3(i == 0, i == 1, i == 2));
	return isidentity;
}

[shader("raygeneration")]
void RayGen()
{
	uint index = DispatchRaysIndex().x;
	Results results = (Results)0;

	Payload payload;
	payload.value = 50;

	RayDesc ray;
	ray.Origin = float3(float(index), 0, 1);
	ray.Direction = float3(0.0, float(index), -1.0);
	ray.TMin = float(index);
	ray.TMax = 10 + float(index);

	// Test basic behavior for a nop or miss object.
	// Hit objects are value types, it's valid to mix and match like this.
	dx::HitObject hit;
	if (index & 1)
		hit = dx::HitObject::MakeMiss(0x2, 1, ray);
	else
		hit = dx::HitObject::MakeNop();

	// Invoking a nop does nothing.
	// Most queries on a nop object return 0 or identity matrices.
	// It's valid to invoke multiple times.

	// NV workaround: Branch over the invoke if it's a nop object.
	// If there is divergence it seems to "call" the null hit object somehow,
	// leading to instant DEVICE_LOST.
	// Keep the crashy code since we need to make sure Vulkan drivers are not broken.
	// NV Vulkan: Also reproduces here.
	if (!hit.IsNop())
	{
		dx::HitObject::Invoke(hit, payload);
		dx::HitObject::Invoke(hit, payload);
	}

	results.data[0] = payload.value;
	results.data[1] = uint(hit.IsMiss());
	results.data[2] = uint(hit.IsNop());
	results.data[3] = uint(hit.GetRayFlags());
	results.data[4] = asuint(hit.GetRayTMin());
	results.data[5] = asuint(hit.GetRayTCurrent());
	results.data[6] = asuint(hit.GetWorldRayOrigin().x);
	results.data[7] = asuint(hit.GetWorldRayOrigin().y);
	results.data[8] = asuint(hit.GetWorldRayOrigin().z);
	results.data[9] = asuint(hit.GetWorldRayDirection().x);
	results.data[10] = asuint(hit.GetWorldRayDirection().y);
	results.data[11] = asuint(hit.GetWorldRayDirection().z);
	results.data[12] = asuint(hit.GetObjectRayOrigin().x);
	results.data[13] = asuint(hit.GetObjectRayOrigin().y);
	results.data[14] = asuint(hit.GetObjectRayOrigin().z);
	results.data[15] = asuint(hit.GetObjectRayDirection().x);
	results.data[16] = asuint(hit.GetObjectRayDirection().y);
	results.data[17] = asuint(hit.GetObjectRayDirection().z);
	results.data[18] = check_identity(hit.GetObjectToWorld3x4());
	results.data[19] = check_identity(hit.GetObjectToWorld4x3());
	results.data[20] = check_identity(hit.GetWorldToObject3x4());
	results.data[21] = check_identity(hit.GetWorldToObject4x3());
	results.data[22] = hit.GetInstanceIndex();
	results.data[23] = hit.GetInstanceID();
	results.data[24] = hit.GetGeometryIndex();
	results.data[25] = hit.GetPrimitiveIndex();
	results.data[26] = hit.GetHitKind();
	Attr attr;
	hit.GetAttributes(attr);
	results.data[27] = attr.attr.x | attr.attr.y;
	// For nop hit objects, this should be ignored.
	hit.SetShaderTableIndex(2);
	results.data[28] = hit.GetShaderTableIndex();

	// Invoke miss shader with different table index.
	// NV workaround: Branch over the invoke if it's a nop object.
	// If there is divergence it seems to "call" the null hit object somehow,
	// leading to instant DEVICE_LOST.
	// Keep the crashy code since we need to make sure Vulkan drivers are not broken.
	// NV Vulkan: Also reproduces here.
	if (!hit.IsNop())
		dx::HitObject::Invoke(hit, payload);
	results.data[29] = payload.value;

	// For nop hit objects, this returns zero. Might need robustness checks?
	results.data[30] = hit.LoadLocalRootTableConstant(4);

	Buf[index] = results;
}
