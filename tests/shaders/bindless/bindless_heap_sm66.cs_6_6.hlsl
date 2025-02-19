cbuffer heap_offsets : register(b0)
{
        uint src_uav_tex_offset;
        uint src_srv_tex_offset;
        uint src_srv_buffer_offset;
        uint dst_uav_buffer_offset;
};

struct Dummy { uint v; };

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        uint input_value = 0;

        if (thr % 2 == 0)
        {
                Texture2D<float> src = ResourceDescriptorHeap[NonUniformResourceIndex(src_srv_tex_offset + thr)];
                SamplerState samp = SamplerDescriptorHeap[NonUniformResourceIndex(thr)];
                input_value += uint(src.SampleLevel(samp, float2(1.25, 1.25), 0.0));
        }
        else
        {
                RWTexture2D<float> src = ResourceDescriptorHeap[NonUniformResourceIndex(src_uav_tex_offset + thr)];
                input_value += uint(src[int2(0, 0)]);
        }

        // Test different descriptor types.
        if (thr % 16 == 0)
        {
                ConstantBuffer<Dummy> src = ResourceDescriptorHeap[NonUniformResourceIndex(src_srv_buffer_offset + thr)];
                input_value += src.v;
        }
        else if (thr % 4 == 0)
        {
                ByteAddressBuffer src = ResourceDescriptorHeap[NonUniformResourceIndex(src_srv_buffer_offset + thr)];
                input_value += src.Load(0);
        }
        else if (thr % 4 == 1)
        {
                Buffer<uint> src = ResourceDescriptorHeap[NonUniformResourceIndex(src_srv_buffer_offset + thr)];
                input_value += src[0];
        }
        else
        {
                StructuredBuffer<uint> dst = ResourceDescriptorHeap[NonUniformResourceIndex(src_srv_buffer_offset + thr)];
                input_value += dst[0];
        }

        // Test different descriptor types.
        if (thr % 4 == 0)
        {
                RWByteAddressBuffer dst = ResourceDescriptorHeap[NonUniformResourceIndex(dst_uav_buffer_offset + thr)];
                dst.Store(0, input_value);
        }
        else if (thr % 4 == 1)
        {
                RWBuffer<uint> dst = ResourceDescriptorHeap[NonUniformResourceIndex(dst_uav_buffer_offset + thr)];
                dst[0] = input_value;
        }
        else
        {
                RWStructuredBuffer<uint> dst = ResourceDescriptorHeap[NonUniformResourceIndex(dst_uav_buffer_offset + thr)];
                dst[0] = input_value;
        }
}