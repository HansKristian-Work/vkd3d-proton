struct VOut
{
        nointerpolation float attr0 : ATTR0;
        float attr1 : ATTR1;
        noperspective float attr2 : ATTR2;
        float4 pos : SV_Position;
};

float4 main(VOut vout,
        float3 bary_perspective : SV_Barycentrics,
        noperspective float3 bary_noperspective : SV_Barycentrics1) : SV_Target
{
        float reference_perspective = vout.attr1;
        float reference_noperspective = vout.attr2;

        float result_perspective =
                GetAttributeAtVertex(vout.attr0, 0) * bary_perspective.x +
                GetAttributeAtVertex(vout.attr0, 1) * bary_perspective.y +
                GetAttributeAtVertex(vout.attr0, 2) * bary_perspective.z;

        float result_noperspective =
                GetAttributeAtVertex(vout.attr0, 0) * bary_noperspective.x +
                GetAttributeAtVertex(vout.attr0, 1) * bary_noperspective.y +
                GetAttributeAtVertex(vout.attr0, 2) * bary_noperspective.z;

        float4 res;
        // Results should be approximately equal.
        res.x = abs(reference_perspective - result_perspective) * 20.0;
        res.y = abs(reference_noperspective - result_noperspective) * 20.0;

        // Test that we can also read the provoking vertex.
        res.z = vout.attr0 / 255.0;

        // Validate barycentrics approximately sum up as expected. Result should be 0x80 when rounded.
        res.w = (64.0 / 255.0) * (dot(bary_perspective, 1.0.xxx) + dot(bary_noperspective, 1.0.xxx));

        return res;
}
