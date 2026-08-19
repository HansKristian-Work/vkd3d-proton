struct [raypayload] Payload
{
	uint value : read(miss, closesthit, anyhit, caller) : write(miss, closesthit, anyhit, caller);
};

struct Results
{
	uint data[32];
};

RWStructuredBuffer<Results> Buf : register(u0);
RaytracingAccelerationStructure RTAS : register(t0);

[shader("closesthit")]
void RayClosest(inout Payload payload, BuiltInTriangleIntersectionAttributes attr)
{
	payload.value += 17;
}

[shader("anyhit")]
void RayAny(inout Payload payload, BuiltInTriangleIntersectionAttributes attr)
{
	payload.value += 19;
	IgnoreHit();
}

[shader("miss")]
void RayMiss(inout Payload payload)
{
	payload.value += 13;
}

[shader("raygeneration")]
void RayGen()
{
	uint index = DispatchRaysIndex().x;
	Results results = (Results)0;

	Payload payload;
	payload.value = 50;

	// Spread the rays out in a grid.
	// Use a small delta offset in X to make the trace stable,
	// e.g. we don't want to fall right on the seam between two triangles.
	RayDesc ray;
	ray.Origin = float3(float(index % 8) / 4.0 + 1.0 / 256.0, float(index / 8) / 4.0 + 1.0 / 256.0, 1);
	ray.Direction = float3(0.0, 0.0, -1.0);
	ray.TMin = 0.0;
	ray.TMax = 2.0;

	dx::HitObject hit = dx::HitObject::TraceRay(RTAS, 0,
		0x1, // InstanceMask
		0, // RayContributionToHitGroup
		1, // MultiplierForGeometry
		0, // MissShaderIndex
		ray, payload);

	// Exercise the three possible variants for reordering.
	// There are no semantic differences we can test for.
	// Just make sure the SPIR-V translates correctly and that drivers don't explode.
	dx::MaybeReorderThread(hit);
	dx::MaybeReorderThread(index, index & 31);
	dx::MaybeReorderThread(hit, index, index & 15);

	results.data[0] = uint(hit.IsMiss()) + uint(hit.IsNop()) * 2 + uint(hit.IsHit()) * 4;
	results.data[1] = asuint(hit.GetRayTCurrent());
	results.data[2] = asuint(hit.GetWorldRayOrigin().x);
	results.data[3] = asuint(hit.GetWorldRayOrigin().y);
	results.data[4] = asuint(hit.GetWorldRayOrigin().z);
	results.data[5] = asuint(hit.GetWorldRayDirection().x);
	results.data[6] = asuint(hit.GetWorldRayDirection().y);
	results.data[7] = asuint(hit.GetWorldRayDirection().z);
	results.data[8] = asuint(hit.GetObjectRayOrigin().x);
	results.data[9] = asuint(hit.GetObjectRayOrigin().y);
	results.data[10] = asuint(hit.GetObjectRayOrigin().z);
	results.data[11] = asuint(hit.GetObjectRayDirection().x);
	results.data[12] = asuint(hit.GetObjectRayDirection().y);
	results.data[13] = asuint(hit.GetObjectRayDirection().z);
	results.data[14] = hit.GetInstanceIndex();
	results.data[15] = hit.GetInstanceID();
	results.data[16] = hit.GetGeometryIndex();
	results.data[17] = hit.GetPrimitiveIndex();
	results.data[18] = hit.GetHitKind();

	BuiltInTriangleIntersectionAttributes attr;
	hit.GetAttributes(attr);

	results.data[19] = asuint(attr.barycentrics.x);
	results.data[20] = asuint(attr.barycentrics.y);

	results.data[21] = hit.GetShaderTableIndex();

	// Get payload values before invoking.
	results.data[22] = payload.value;

	// Invoke closesthit (if present) twice.
	// It's possible to invoke with a different payload than during trace.
	Payload alt = payload;
	alt.value -= 10;
	dx::HitObject::Invoke(hit, alt);

	// Replace some hits with misses and re-invoke.
	// Funny things might happen!
	if (hit.GetShaderTableIndex() == 0 && hit.IsHit())
		hit = dx::HitObject::MakeMiss(0, 0, ray);

	dx::HitObject::Invoke(hit, payload);

	results.data[23] = payload.value;
	results.data[24] = alt.value;

	results.data[25] = hit.LoadLocalRootTableConstant(4);

	// Hits are supposed to only read lower 28 bits, misses only read lower 16 bits.
	if (hit.IsHit())
		hit.SetShaderTableIndex((0xfu << 28) | 3);
	else
		hit.SetShaderTableIndex(0xffffu << 16);

	payload.value = 0;

	// Check that we invoke hit 3 or miss 0.
	dx::HitObject::Invoke(hit, payload);
	results.data[26] = hit.LoadLocalRootTableConstant(4);
	results.data[27] = payload.value;

	Buf[index] = results;
}
