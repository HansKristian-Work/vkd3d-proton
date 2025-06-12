float4 main(in uint id : SV_VERTEXID, in uint in_view_id : SV_VIEWID,
    out uint out_view_id : UV_VIEWID) : SV_POSITION
{
    out_view_id = in_view_id;
    float2 coords = float2(id & 2, (id << 1) & 2);
    return float4(coords * float2(2, 2) + float2(-1, -1), 0, 1);
}
