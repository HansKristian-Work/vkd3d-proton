RaytracingAccelerationStructure AS : register(t0);
RWStructuredBuffer<float> Buf : register(u0);

Texture2D<float> Tex : register(t1);
SamplerState ClampSampler : register(s0);
SamplerState WrapSampler : register(s1);
SamplerState ClampWrapSampler : register(s2);

struct RayPayload
{
    float value;
};

[shader("closesthit")]
void RayClosest1(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    /* Tex(1, 1) + Tex(1, 0) = 40 + 20 = 60 */
    payload.value =
            Tex.SampleLevel(ClampSampler, 1.25.xx, 0.0) +
            Tex.SampleLevel(ClampWrapSampler, 1.25.xx, 0.0);
}

[shader("closesthit")]
void RayClosest2(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    /* Tex(0, 0) + Tex(1, 0) = 10 + 20 = 30 */
    payload.value =
            Tex.SampleLevel(WrapSampler, 1.25.xx, 0.0) +
            Tex.SampleLevel(ClampWrapSampler, 1.25.xx, 0.0);
}

[shader("miss")]
void RayMiss(inout RayPayload payload)
{
    payload.value = -1.0;
}

[shader("raygeneration")]
void RayGen()
{
    RayPayload payload;
    payload.value = 0.0;
    uint2 index = DispatchRaysIndex().xy;
    RayDesc ray;
    ray.Origin = float3(10.0 * float2(index), 1.0);
    ray.Direction = float3(0.0, 0.0, -1.0);
    ray.TMin = 0;
    ray.TMax = 10;

    TraceRay(AS, RAY_FLAG_NONE, 1, 0, 1, 0, ray, payload);

    Buf[index.y * 2 + index.x] = payload.value;
}