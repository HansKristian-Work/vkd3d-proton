struct VOut
{
        nointerpolation float attr0 : ATTR0;
        float attr1 : ATTR1;
        noperspective float attr2 : ATTR2;
        float4 pos : SV_Position;
};
VOut main(float3 pos : POSITION, float attr : ATTR)
{
        VOut vout;
        vout.pos = float4(pos.xy * pos.z, 0.0, pos.z);
        vout.attr0 = attr;
        vout.attr1 = attr;
        vout.attr2 = attr;
        return vout;
}
