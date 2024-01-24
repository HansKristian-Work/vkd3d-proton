RaytracingAccelerationStructure AS : register(t0);
RWStructuredBuffer<uint> Buf0 : register(u0);
RWStructuredBuffer<uint> Buf1 : register(u1);

struct Payload
{
	float dummy;
};

[shader("miss")]
void MissShader(inout Payload payload)
{
	payload.dummy = 1.0;
	uint o;
	InterlockedAdd(Buf0[0], 100, o);
	InterlockedAdd(Buf1[0], 200, o);
}

[shader("raygeneration")]
void GenShader()
{
	Payload payload;
	payload.dummy = 0.0;

        RayDesc ray;
        ray.Origin = -1000.0.xxxx;
        ray.Direction = float3(1.0, 0.0, 0.0);
        ray.TMin = 0;
        ray.TMax = 0.01;

	// Trigger miss shader
        TraceRay(AS, 0,
                0x01, // mask
                0, // HitGroup offset
                1, // geometry contribution multiplier
                0, // miss shader index
                ray, payload);

	uint o;
	InterlockedAdd(Buf0[0], 1000, o);
	InterlockedAdd(Buf1[0], 2000, o);
}

