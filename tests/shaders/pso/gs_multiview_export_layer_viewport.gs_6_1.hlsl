struct VertexData
{
    float4 pos : SV_POSITION;
    uint rt_data : UV_VIEWID;
    uint layer : SV_RENDERTARGETARRAYINDEX;
    uint viewport : SV_VIEWPORTARRAYINDEX;
};

[maxvertexcount(3)]
void main(in uint view_id : SV_VIEWID, triangle VertexData vtx_in[3], inout TriangleStream<VertexData> vtx_out)
{
    for (uint i = 0; i < 3; i++)
    {
        VertexData result;
        result.pos = vtx_in[i].pos;
        result.layer = vtx_in[i].layer;
        result.viewport = vtx_in[i].viewport;
        result.rt_data = view_id | (result.layer << 8) | (result.viewport << 16);

        vtx_out.Append(result);
    }
}
