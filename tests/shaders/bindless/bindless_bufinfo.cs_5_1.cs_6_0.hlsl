RWStructuredBuffer<uint> RWBuf[] : register(u0, space0);
RWTexture2D<uint> RWTex[] : register(u0, space1);

[numthreads(64, 1, 1)]
void main(uint2 thr : SV_DispatchThreadID)
{
    uint width, height, count, stride;
    RWTex[NonUniformResourceIndex(thr.x)].GetDimensions(width, height);

    if (thr.y == 0)
        RWTex[NonUniformResourceIndex(thr.x)][int2(0, 0)] = width;

    RWBuf[NonUniformResourceIndex(thr.x)].GetDimensions(count, stride);
    if (thr.y == 0)
        RWBuf[NonUniformResourceIndex(thr.x)][0] = count;
}