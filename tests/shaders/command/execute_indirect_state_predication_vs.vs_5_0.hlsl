cbuffer C0 : register(b0)
{
    uint offset;
};

cbuffer C1 : register(b1)
{
    uint multiplier;
};

RWStructuredBuffer<uint> U : register(u0);

float4 main(uint vid : SV_VertexID) : SV_Position
{
        float2 pos;
        pos.x = float(vid & 1) * 4.0 - 1.0;
        pos.y = float(vid & 2) * 2.0 - 1.0;
        return float4(pos.x, pos.y, 0.0, 1.0);
}
