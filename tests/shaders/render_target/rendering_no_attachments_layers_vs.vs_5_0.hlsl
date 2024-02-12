struct VOut
{
    float4 pos : SV_Position;
    uint layer : SV_RenderTargetArrayIndex;
};

VOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VOut v;
    if (vid == 0)
        v.pos = float4(-1, -1, 0, 1);
    else if (vid == 1)
        v.pos = float4(-1, +3, 0, 1);
    else
        v.pos = float4(+3, -1, 0, 1);
    v.layer = iid;
    return v;
}