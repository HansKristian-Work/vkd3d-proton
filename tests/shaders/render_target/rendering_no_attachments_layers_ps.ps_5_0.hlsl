struct VOut
{
    float4 pos : SV_Position;
    uint layer : SV_RenderTargetArrayIndex;
};

RWStructuredBuffer<float4> RWBuf : register(u0);
void main(VOut pin)
{
    int2 coord = int2(pin.pos.xy) - int2(10000, 12000);
    int write_coord = 16 * pin.layer + coord.y * 4 + coord.x;
    float4 write_value = float4(pin.pos.xy, pin.layer, 0.0);
    RWBuf[write_coord] = write_value;
}