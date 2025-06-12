cbuffer globals : register(b0)
{
    uint layer;
    uint viewport;
};

struct VertexData
{
    float4 pos : SV_POSITION;
    uint rt_data : UV_VIEWID;
    uint layer : SV_RENDERTARGETARRAYINDEX;
    uint viewport : SV_VIEWPORTARRAYINDEX;
};

VertexData main(in uint id : SV_VERTEXID)
{
    VertexData result;
    /* no view ID here */
    result.rt_data = (layer << 8) | (viewport << 16);
    result.layer = layer;
    result.viewport = viewport;

    float2 coords = float2(id & 2, (id << 1) & 2);
    result.pos = float4(coords * float2(2, 2) + float2(-1, -1), 0, 1);
    return result;
}
