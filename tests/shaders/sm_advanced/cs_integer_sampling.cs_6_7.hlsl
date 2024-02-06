RWStructuredBuffer<uint4> OutputData : register(u0);

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s4 : register(s4);

uint4 samp(uint tex_index, SamplerState s)
{
    uint4 result;
    if (tex_index == 6)
    {
        Texture2D<uint2> utex = ResourceDescriptorHeap[tex_index];
        result.x = utex.SampleLevel(s, float2(-0.4, -0.4), 0.0).g;
        result.y = utex.SampleLevel(s, float2(0.1, 0.1), 0.0).g;
        result.z = utex.SampleLevel(s, float2(0.6, 0.6), 0.0).g;
        result.w = utex.SampleLevel(s, float2(1.1, 1.1), 0.0).g;
    }
    else if ((tex_index & 1) == 0)
    {
        Texture2D<uint> utex = ResourceDescriptorHeap[tex_index];
        result.x = utex.SampleLevel(s, float2(-0.4, -0.4), 0.0);
        result.y = utex.SampleLevel(s, float2(0.1, 0.1), 0.0);
        result.z = utex.SampleLevel(s, float2(0.6, 0.6), 0.0);
        result.w = utex.SampleLevel(s, float2(1.1, 1.1), 0.0);
    }
    else
    {
        Texture2D<int> itex = ResourceDescriptorHeap[tex_index];
        result.x = itex.SampleLevel(s, float2(-0.4, -0.4), 0.0);
        result.y = itex.SampleLevel(s, float2(0.1, 0.1), 0.0);
        result.z = itex.SampleLevel(s, float2(0.6, 0.6), 0.0);
        result.w = itex.SampleLevel(s, float2(1.1, 1.1), 0.0);
    }
    return result;
}

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint4 result;
    uint tex_index = id.x;
    uint sampler_index = id.y;

    if (sampler_index >= 5)
    {
        sampler_index -= 5;
        // If we're testing border color, test something that is in range for the type in question.
        if (sampler_index == 3)
            sampler_index += tex_index;
        SamplerState s = SamplerDescriptorHeap[sampler_index];
        result = samp(tex_index, s);
    }
    else if (sampler_index == 0)
        result = samp(tex_index, s0);
    else if (sampler_index == 1)
        result = samp(tex_index, s1);
    else if (sampler_index == 2)
        result = samp(tex_index, s2);
    else if (sampler_index == 3)
        result = samp(tex_index, s3);
    else
        result = samp(tex_index, s4);

    OutputData[id.y * 7 + id.x] = result;
}
