Texture2D<uint4> t;
RWTexture2D<uint4> u;

[numthreads(1, 1, 1)]
void main(uint2 id : SV_GroupID)
{
    u[id] = t[id];
}
