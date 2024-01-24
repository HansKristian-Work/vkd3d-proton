RaytracingAccelerationStructure AS : register(t0);
StructuredBuffer<float2> RayPositions : register(t1);
RWStructuredBuffer<float2> Buf : register(u0);

cbuffer Flags : register(b0, space0)
{
    float2 miss_color;
    uint flags;
};

[numthreads(64, 1, 1)]
void main(uint index : SV_DispatchThreadID)
{
    float2 color = float2(0.0, 0.0);

    RayDesc ray;
    ray.Origin = float3(RayPositions[index], 1.0);
    ray.Direction = float3(0.0, 0.0, -1.0);
    ray.TMin = 0;
    ray.TMax = 10;

    RayQuery<RAY_FLAG_NONE> rq;
    rq.TraceRayInline(AS, flags, 0x01, ray);

    while (rq.Proceed())
    {
        if (rq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            // Synthesize any-hit shader.
            color.x += 1.0f;
            // Ignore hit, don't commit.

            // Just test that this doesn't screw up anything.
            rq.Abort();
        }
        else
        {
            // Synthesize intersection shader, generate hit at 1.0f.

            // Synthesize any-hit shader and skip it, otherwise, commit.
            if (rq.CandidateProceduralPrimitiveNonOpaque())
                color.y += 1.0f;
            else if (1.0f < rq.CommittedRayT())
                rq.CommitProceduralPrimitiveHit(1.0f);
        }
    }

    if (rq.CommittedStatus() == COMMITTED_NOTHING)
    {
        // Synthesize miss shader.
        color += miss_color;
    }
    else
    {
        // Synthesize closest hit shader.
        uint hit_group_index = rq.CommittedInstanceContributionToHitGroupIndex();
        hit_group_index += rq.CommittedGeometryIndex();
        color += float2(hit_group_index + 1, hit_group_index + 2);
    }

    Buf[index] = color;
}
