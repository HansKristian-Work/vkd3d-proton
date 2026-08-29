RWStructuredBuffer<float> dst_data : register(u0);

Texture2D<float> src_image : register(t0);

[numthreads(8,8,1)]
void main(uint3 gid : SV_DispatchThreadID)
{
    uint w, h, l;
    src_image.GetDimensions(0, w, h, l);

    uint dst_index = w * gid.y + gid.x;

    if (gid.x < w && gid.y < h)
        dst_data[dst_index] = src_image.Load(int3(gid));
}
