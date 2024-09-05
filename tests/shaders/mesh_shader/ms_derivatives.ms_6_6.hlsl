struct vtx
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

uint compute_vertex_index(uint2 coord)
{
    return coord.x + coord.y * 16u;
}

uint compute_primitive_index(uint2 coord)
{
    return 2u * (coord.x + coord.y * 15u);
}

[numthreads(16,8,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex,
    in uint3 thr : SV_DispatchThreadID,
    out vertices vtx v[128],
    out indices uint3 i[210])
{
    SetMeshOutputCounts(128, 210);

    float2 coord = float2(thr.xy) / float2(15.0f, 7.0f);

    uint vid = compute_vertex_index(thr.xy);

    v[vid].position = float4(2.0f * coord - 1.0f, 0.0f, 1.0f);
    v[vid].color = abs(float4(
        ddx_fine(coord.x), ddy_fine(coord.x),
        ddx_fine(coord.y), ddy_fine(coord.y)));

    if (thr.x < 15 && thr.y < 7) {
      uint pid = compute_primitive_index(thr.xy);

      uint4 quad = uint4(
          compute_vertex_index(thr.xy + uint2(0, 0)),
          compute_vertex_index(thr.xy + uint2(1, 0)),
          compute_vertex_index(thr.xy + uint2(0, 1)),
          compute_vertex_index(thr.xy + uint2(1, 1)));

      i[pid + 0] = quad.xyz;
      i[pid + 1] = quad.wzy;
    }
}
