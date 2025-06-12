struct Payload
{
    float4 color;
};

struct Prim
{
    float4 color : UV_COLOR;
};

[numthreads(1,1,1)]
[outputtopology("triangle")]
void main(in uint tid : SV_GroupIndex, in payload Payload in_p,
        out vertices float4 v[3] : SV_POSITION, out primitives Prim p[1], out indices uint3 i[1])
{
    SetMeshOutputCounts(3, 1);
    v[0] = float4(-1.0f, -1.0f, 0.0f, 1.0f);
    v[1] = float4( 3.0f, -1.0f, 0.0f, 1.0f);
    v[2] = float4(-1.0f,  3.0f, 0.0f, 1.0f);
    i[0] = uint3(0, 1, 2);
    p[0].color = in_p.color;
}
