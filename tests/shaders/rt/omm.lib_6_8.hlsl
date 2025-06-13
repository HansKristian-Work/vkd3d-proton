RaytracingAccelerationStructure AS : register(t0);
StructuredBuffer<float4> Origins : register(t1);
RWStructuredBuffer<float> Buf : register(u0);

cbuffer Constants : register(b0)
{
    uint flags;
};

struct [raypayload] Payload
{
    float hits : write(miss, caller, closesthit, anyhit) : read(caller, miss);
};

[shader("miss")]
void MissShader(inout Payload payload)
{
    payload.hits += 0.125;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hits = 1.0;
}

[shader("anyhit")]
void AnyHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hits = 0.75;
    IgnoreHit();
}

[shader("raygeneration")]
void GenShader()
{
    Payload payload;
    payload.hits = 0.0;

    float4 origin = Origins[DispatchRaysIndex().x];

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0;
    ray.TMax = origin.w;

    // Trigger miss shader
    TraceRay(AS, flags,
        0x01, // mask
        0, // HitGroup offset
        1, // geometry contribution multiplier
        0, // miss shader index
        ray, payload);

    Buf[DispatchRaysIndex().x] = payload.hits;
}

