RWStructuredBuffer<uint> RWBuf : register(u0);
RaytracingAccelerationStructure AS : register(t0);

struct Payload
{
	uint value;
};

[shader("miss")]
void Miss1(inout Payload payload)
{
	payload.value = 5;
}

[shader("miss")]
void Miss2(inout Payload payload)
{
	payload.value = 7;
}

[shader("raygeneration")]
void RayGen()
{
        Payload payload;
        payload.value = 10;

        uint index = DispatchRaysIndex().x;

        RayDesc ray;
        ray.Origin = 0.0.xxx;
        ray.Direction = float3(0.0, 0.0, -1.0);
        ray.TMin = 0;
        ray.TMax = 10;

        TraceRay(AS, 0,
                0x01, // mask
                0, // HitGroup offset
                1, // geometry contribution multiplier
                index, // miss shader index
                ray, payload);

        RWBuf[index] = payload.value;
}
