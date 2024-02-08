RWByteAddressBuffer u;

uint location;

float4 main(float4 position : SV_Position) : SV_Target
{
    return u.Load(4 * location);
}