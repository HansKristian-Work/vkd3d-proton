RWStructuredBuffer<uint2> uav : register(u0);

Buffer<float4> buf : register(t0);
Texture2D<float4> tex : register(t1);

cbuffer args : register(b0)
{
    uint2 tex_page_size;
    uint2 tex_size;
    uint tex_mips;
    uint buf_stride;
};

groupshared uint buffer_feedback[8];
groupshared uint texture_feedback[8];

[numthreads(4, 4, 16)]
void main(uint3 gid : SV_DISPATCHTHREADID, uint tid : SV_GROUPINDEX)
{
    if (tid < 8u)
    {
        buffer_feedback[tid] = 0u;
        texture_feedback[tid] = 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    uint dword_index = tid >> 5;
    uint dword_bit = tid & 31;

    uint fb;
    float4 v = buf.Load(tid.x * buf_stride, fb);

    if (!CheckAccessFullyMapped(fb))
      InterlockedOr(buffer_feedback[dword_index], 1u << dword_bit);

    uint2 pos = tex_page_size * gid.xy;
    uint mip = gid.z;
    uint2 mip_size = max(tex_size >> mip, 1u.xx);

    if (mip < tex_mips && pos.x < mip_size.x && pos.y < mip_size.y)
    {
        v = tex.Load(int3(pos, mip), int2(0, 0), fb);

        if (!CheckAccessFullyMapped(fb))
          InterlockedOr(texture_feedback[dword_index], 1u << dword_bit);
    }

    GroupMemoryBarrierWithGroupSync();

    if (tid < 8u)
        uav[tid] = uint2(buffer_feedback[tid], texture_feedback[tid]);
}
