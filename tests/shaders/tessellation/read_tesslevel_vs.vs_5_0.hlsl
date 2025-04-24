struct data
{
    float4 position : SV_Position;
};
data main()
{
    data d;
    d.position = float4(1.0, 2.0, 3.0, 4.0);
    return d;
}