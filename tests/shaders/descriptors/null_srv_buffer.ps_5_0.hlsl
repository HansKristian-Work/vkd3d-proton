ByteAddressBuffer t;

uint location;

float4 main(float4 position : SV_Position) : SV_Target
{
    return t.Load(location);
}