cbuffer RootCBV : register(b0)
{
        float a;
};

StructuredBuffer<float> RootSRV : register(t0);

cbuffer RootConstants : register(b0, space1)
{
        float4 root;
};

float4 main(float c0 : COLOR0, float c1 : COLOR1, uint iid : SV_InstanceID) : SV_Position
{
        return float4(c0, c1, a, RootSRV[0] + float(iid)) + root;
}