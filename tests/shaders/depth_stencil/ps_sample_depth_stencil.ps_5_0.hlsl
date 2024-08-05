SamplerState samp;
Texture2D depth_tex;
Texture2D<uint4> stencil_tex;

float main(float4 position: SV_Position) : SV_Target
{
    float2 s, p;
    float depth, stencil;
    depth_tex.GetDimensions(s.x, s.y);
    p = float2(s.x * position.x / 640.0f, s.y * position.y / 480.0f);
    depth = depth_tex.Sample(samp, p).r;
    stencil = stencil_tex.Load(int3(float3(p.x, p.y, 0))).y;
    return depth + stencil;
}
