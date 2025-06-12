float4 main(uint vid : SV_VertexID) : SV_Position
{
    if (vid == 0)
        return float4(-1.0, -1.0, 0.0, 1.0);
    else if (vid == 1)
        return float4(3.0, -1.0, 0.0, 1.0);
    else
        return float4(-1.0, 3.0, 0.0, 1.0);
}
