float build_alpha_shuffle(float v)
{
    v = IsHelperLane() ? 8.0 : round(v);
    float4 helpers;
    helpers.x = QuadReadLaneAt(v, 0);
    helpers.y = QuadReadLaneAt(v, 1);
    helpers.z = QuadReadLaneAt(v, 2);
    helpers.w = QuadReadLaneAt(v, 3);
    return dot(helpers, float4(1, 10, 100, 1000));
}

float4 main(float4 pos : SV_Position) : SV_Target
{
    int2 coord = int2(pos.xy);
    int linear_coord = coord.y * 2 + coord.x;

    StructuredBuffer<float> alpha = ResourceDescriptorHeap[0];
    float alpha_value = alpha[linear_coord];
    float mask0 = build_alpha_shuffle(alpha_value);
    // Lane 1 and 2 should be nuked by this.
    if (frac(alpha_value) < 0.5)
        discard;

    float mask1 = build_alpha_shuffle(alpha_value);

    RWStructuredBuffer<uint> atomics = ResourceDescriptorHeap[1];
    uint last_value = 0;
    InterlockedAdd(atomics[linear_coord], 101, last_value);
    if (linear_coord == 3 || last_value > 1000)
        discard;

    float mask2 = build_alpha_shuffle(alpha_value);
    float4 color = float4(1.0, mask0, mask1, mask2);
    return color;
}
