void main(float4 in_position : POSITION, uint4 in_uint : UINT,
        out float4 out_position : SV_POSITION, out uint out_uint : UINT,
        out float3 out_float : FLOAT)
{
    out_position = in_position;
    out_uint = in_uint.y;
    out_float = float3(1, 2, 3);
}
