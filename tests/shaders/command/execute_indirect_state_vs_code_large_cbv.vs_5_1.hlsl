cbuffer RootCBV : register(b0)
{
    float a;
};

StructuredBuffer<float> RootSRV : register(t0);

cbuffer RootConstants : register(b0, space1)
{
    // Cannot use arrays for root constants in D3D12.
    float4 pad0, pad1, pad2, pad3, pad4, pad5, pad6, pad7, pad8, pad9, pad10;
    float4 root;
};

float4 main(float c0 : COLOR0, float c1 : COLOR1, uint iid : SV_InstanceID) : SV_Position
{
    return float4(c0, c1, a, RootSRV[0] + float(iid)) + root;
}