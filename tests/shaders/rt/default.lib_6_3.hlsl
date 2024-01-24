RaytracingAccelerationStructure AS : register(t0);
StructuredBuffer<float2> RayPositions : register(t1);
RWStructuredBuffer<float2> Buf : register(u0);

struct RayPayload
{
        float2 color;
};

cbuffer LocalConstants : register(b0, space1)
{
        float local_value0;
};

cbuffer LocalConstants2 : register(b1, space1)
{
        float local_value1;
};

struct AABBAttribs { float dummy; };

[shader("miss")]
void RayMiss(inout RayPayload payload)
{
        payload.color.x += local_value0;
        payload.color.y += local_value1;
}

[shader("closesthit")]
void RayClosest(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
        payload.color.x += local_value0;
        payload.color.y += local_value1;
}

[shader("anyhit")]
void RayAnyTriangle(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
        payload.color.x += 1.0;
        IgnoreHit();
}

[shader("anyhit")]
void RayAnyAABB(inout RayPayload payload, AABBAttribs attribs)
{
        payload.color.y += attribs.dummy;
        IgnoreHit();
}

[shader("intersection")]
void RayIntersect()
{
        AABBAttribs dummy;
        dummy.dummy = 1.0;
        float thit = 1.0; // Hardcode an intersection at origin of the AABB.
        ReportHit(thit, 0, dummy);
}

cbuffer Flags : register(b0, space0) { uint flags; };

[shader("raygeneration")]
void RayGen()
{
        RayPayload payload;
        payload.color = float2(0.0, 0.0);

        uint index = DispatchRaysIndex().x;

        RayDesc ray;
        ray.Origin = float3(RayPositions[index], 1.0);
        ray.Direction = float3(0.0, 0.0, -1.0);
        ray.TMin = 0;
        ray.TMax = 10;

        TraceRay(AS, flags,
                0x01, // mask
                0, // HitGroup offset
                1, // geometry contribution multiplier
                0, // miss shader index
                ray, payload);

        Buf[index] = payload.color;
}