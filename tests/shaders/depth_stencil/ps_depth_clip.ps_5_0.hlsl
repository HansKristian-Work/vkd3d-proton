float depth;

float4 main(float4 p : SV_Position, out float out_depth : SV_Depth) : SV_Target
{
    out_depth = depth;
    return float4(0, 1, 0, 1);
}
