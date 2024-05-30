void main(in uint shading_rate : SV_ShadingRate, out float4 o0 : SV_Target0)
{
    const uint D3D12_SHADING_RATE_VALID_MASK = 0x3;
    const uint D3D12_SHADING_RATE_X_AXIS_SHIFT = 2;
    o0 = float4(
            ((shading_rate >> D3D12_SHADING_RATE_X_AXIS_SHIFT) & D3D12_SHADING_RATE_VALID_MASK) / 255.0,
            (shading_rate & D3D12_SHADING_RATE_VALID_MASK) / 255.0,
            0.0,
            0.0);
}