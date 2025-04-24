struct data
{
    float4 position : SV_Position;
    float4 a : TEXCOORD0;
    float4 b : TEXCOORD1;
    float4 c : TEXCOORD2;
};
data main(uint vid : SV_VertexID)
{
    data d;
    d.a = (100.0 / 255.0).xxxx;
    d.b = (200.0 / 255.0).xxxx;
    d.c = (155.0 / 255.0).xxxx;

    if (vid == 0)
        d.position = float4(-1, -1, 0, 1);
    else if (vid == 1)
        d.position = float4(-1, 3, 0, 1);
    else
        d.position = float4(3, -1, 0, 1);

    return d;
}