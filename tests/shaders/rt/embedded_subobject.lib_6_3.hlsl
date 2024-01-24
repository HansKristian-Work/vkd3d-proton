RWStructuredBuffer<float> RW1 : register(u0);
RWStructuredBuffer<float> RW2 : register(u1);
RWStructuredBuffer<float> RW3 : register(u2);
RWStructuredBuffer<float> RW4 : register(u3);

RaytracingShaderConfig config = { 4, 8 };
RaytracingPipelineConfig pconfig = { 1 };

GlobalRootSignature grs = { "UAV(u0)" };
GlobalRootSignature grs_dummy = { "UAV(u10)" };
LocalRootSignature lrs_raygen = { "UAV(u1)" };
LocalRootSignature lrs_other1 = { "UAV(u2)" };
LocalRootSignature lrs_other2 = { "UAV(u3)" };

TriangleHitGroup thg = { "", "RayClosestHit" };
ProceduralPrimitiveHitGroup phg = { "", "RayClosestHitAABB", "RayIntersect" };

SubobjectToExportsAssociation exports1 = { "grs", "thg;phg;RayGen" };
SubobjectToExportsAssociation exports2 = { "lrs_raygen", "RayGen" };
SubobjectToExportsAssociation exports3 = { "lrs_other1", "thg" };
SubobjectToExportsAssociation exports4 = { "lrs_other2", "phg" };

struct Payload { float i; };
struct Attr { float v; };

[shader("closesthit")]
void RayClosestHit(inout Payload p, BuiltInTriangleIntersectionAttributes a)
{
        p.i = RW3[0] + RW1[0];
}

[shader("closesthit")]
void RayClosestHitAABB(inout Payload p, Attr a)
{
        p.i = RW4[0] + a.v + RW1[0];
}

[shader("intersection")]
void RayIntersect()
{
        Attr v;
        ReportHit(float(RW4[0]), 0, v);
}

[shader("raygeneration")]
void RayGen()
{
        RW1[0] = 0;
        RW2[0] = 0;
}