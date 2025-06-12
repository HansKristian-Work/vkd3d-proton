struct out_t
{
    float d : SV_DEPTH;
    float f : SV_TARGET0;
    uint u : SV_TARGET1;
    int s : SV_TARGET2;
};

out_t main(in float4 pos : SV_Position, in uint sample_id : SV_SampleIndex)
{
    uint value = (sample_id ^ 1) + 4 * uint(pos.x) + 16 * uint(pos.y);
    out_t result;
    result.d = float(value) / 64.0f;
    result.f = float(value) / 64.0f;
    result.u = value;
    result.s = int(value) - 32;
    return result;
}
