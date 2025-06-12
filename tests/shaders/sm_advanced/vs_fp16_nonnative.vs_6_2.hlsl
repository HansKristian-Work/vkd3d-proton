struct VOut
{
    float4 pos : SV_Position;
    min16float2 v : V;
};

VOut main(uint vert : SV_VertexID)
{
    VOut vout;
    if (vert == 0) vout.pos = float4(-1, -1, 0, 1);
    else if (vert == 1) vout.pos = float4(-1, 3, 0, 1);
    else vout.pos = float4(3, -1, 0, 1);
    vout.v = min16float2(vout.pos.xy * 0.5 + 0.5);
    return vout;
}
