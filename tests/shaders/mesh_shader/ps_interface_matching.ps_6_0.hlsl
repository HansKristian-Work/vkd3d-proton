RWStructuredBuffer<uint> uav : register(u1);

float4 main(in float2 coord : UV_COORD,
        nointerpolation in float4 color : UV_COLOR,
        nointerpolation in uint prim_data : UV_PRIMITIVE_DATA,
        nointerpolation uint id : UV_VERTEX_ID) : SV_TARGET0
{
    uint2 r = uint2(round(2.0f * coord));

    InterlockedOr(uav[0], prim_data);
    InterlockedOr(uav[1], r.x);
    InterlockedOr(uav[2], r.y);
    InterlockedOr(uav[3], id);
    return color;
}
