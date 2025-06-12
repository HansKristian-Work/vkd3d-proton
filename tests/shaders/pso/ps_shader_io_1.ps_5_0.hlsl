void main(float4 position : SV_POSITION, uint in_uint : UINT,
        float3 in_float : FLOAT, out float4 out_float : SV_TARGET0,
        out uint4 out_uint : SV_TARGET1)
{
    out_float.x = position.w;
    out_float.y = in_uint;
    out_float.z = in_float.z;
    out_float.w = 0;
    out_uint.x = 0xdeadbeef;
    out_uint.y = 0;
    out_uint.z = in_uint;
    out_uint.w = in_float.z;
}
