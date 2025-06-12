void main(uint id : SV_VertexID, out uint shading_rate : SV_ShadingRate, out float4 position : SV_Position)
{
    shading_rate = 0x1; // 1x2
    float2 coords = float2((id << 1) & 2, id & 2);
    position = float4(coords * float2(2, -2) + float2(-1, 1), 0, 1);
}