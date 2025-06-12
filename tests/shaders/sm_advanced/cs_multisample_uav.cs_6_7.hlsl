RWTexture2DMS<float, 4> FirstLayer : register(u0);
RWTexture2DMSArray<float, 4> Layers : register(u1);
RWStructuredBuffer<float4> Outputs : register(u2);

float compute_reference_value(uint3 coord, uint sample)
{
        return float(coord.z * 64 + coord.y * 16 + coord.x * 4 + sample);
}

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Read and write to different coordinates so we're sure we're going through the descriptors.
    uint3 write_coord = id ^ uint3(1, 2, 3);
    if (write_coord.z == 0)
    {
            FirstLayer[write_coord.xy] = compute_reference_value(write_coord, 0);
            for (int i = 1; i < 4; i++)
                    FirstLayer.sample[i][write_coord.xy] = compute_reference_value(write_coord, i);
    }
    else
    {
            Layers[write_coord] = compute_reference_value(write_coord, 0);
            for (int i = 1; i < 4; i++)
                    Layers.sample[i][write_coord] = compute_reference_value(write_coord, i);
    }

    // globallycoherent is not needed since it's within same threadgroup.
    DeviceMemoryBarrierWithGroupSync();

    // Test all new commands.
    float4 samples;
    if (id.z == 0)
    {
            samples[0] = FirstLayer[id.xy];
            // Make sure the sample indexing works both in a constant and dynamic context.
            [unroll]
            for (int i = 1; i < 4; i++)
                    samples[i] = FirstLayer.sample[i][id.xy];
    }
    else
    {
            samples[0] = Layers[id];
            [loop]
            for (int i = 1; i < 4; i++)
                    samples[i] = Layers.sample[i][id];
    }

    Outputs[id.z * 16 + id.y * 4 + id.x] = samples;
}
