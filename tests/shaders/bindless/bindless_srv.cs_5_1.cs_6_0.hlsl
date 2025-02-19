// Space 1 and 2 have an offset of 0 descriptors, so pattern of descriptors is
// [ buf, tex, buf, tex ]
StructuredBuffer<uint> Buffers[] : register(t4, space1);
Texture2D<uint> Textures[] : register(t4, space2);

// Space 3 and 4 have an effective offset of 2 descriptor,
// so pattern of descriptors is still
// [ buf, tex, buf, tex ]
StructuredBuffer<uint> AliasBuffers[64] : register(t4, space3);
Texture2D<uint> AliasTextures[64] : register(t4, space4);

StructuredBuffer<uint> StandaloneBuffer : register(t100, space3);
Texture2D<uint> StandaloneTexture : register(t199, space3);

RWByteAddressBuffer OBuffer : register(u0);

[numthreads(64, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
    uint result = 0;

    if (idx & 1)
        result += Textures[NonUniformResourceIndex(idx)].Load(int3(0, 0, 0));
    else
        result += Buffers[NonUniformResourceIndex(idx)].Load(0);

    if (idx & 1)
        result += AliasTextures[NonUniformResourceIndex(idx)].Load(int3(0, 0, 0)) << 8;
    else
        result += AliasBuffers[NonUniformResourceIndex(idx)].Load(0) << 8;

    result *= StandaloneBuffer.Load(0);
    result *= StandaloneTexture.Load(int3(0, 0, 0));
    OBuffer.Store(4 * idx, result);
}