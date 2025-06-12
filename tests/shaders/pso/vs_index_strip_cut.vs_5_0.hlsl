RWStructuredBuffer<uint> results : register(u0);

cbuffer push_args
{
    uint test_index;
    uint strip_cut_value;
};

float4 main(in uint id : SV_VERTEXID) : SV_POSITION
{
    if (id == strip_cut_value)
        results[test_index] = 1u;

    return 0.0f.xxxx;
}
