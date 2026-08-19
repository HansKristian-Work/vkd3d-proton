RaytracingAccelerationStructure AS : register(t0);
StructuredBuffer<float4> Origins : register(t1);
RWStructuredBuffer<float> Buf : register(u0);

cbuffer Constants : register(b0)
{
	uint flags;
};

struct Payload
{
	float hits;
};

void MissShader(inout Payload payload)
{
	payload.hits += 0.125;
}

void ClosestHit(inout Payload payload)
{
	payload.hits = 1.0;
}

void AnyHit(inout Payload payload)
{
	payload.hits = 0.75;
}

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Payload payload;
	payload.hits = 0.0;

	float4 origin = Origins[thr];

	RayDesc ray;
	ray.Origin = origin.xyz;
	ray.Direction = float3(0.0, 0.0, 1.0);
	ray.TMin = 0;
	ray.TMax = origin.w;

	RayQuery<RAY_FLAG_NONE, RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS> rq;
	rq.TraceRayInline(AS, flags, 0x01, ray);

	// Register the anyhit, but don't commit it as closest hit.
	while (rq.Proceed())
		if (rq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
			AnyHit(payload);

	if (rq.CommittedStatus() == COMMITTED_NOTHING)
		MissShader(payload);
	else
		ClosestHit(payload);

	Buf[thr] = payload.hits;
}

