struct VertexData
{
    float4 pos : SV_POSITION;
    uint rt_data : UV_VIEWID;
};

uint main(in VertexData vertex_data) : SV_TARGET0
{
    return vertex_data.rt_data;
}
