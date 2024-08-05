Texture2D<float> t;
RWTexture2D<float> u;

[numthreads(1, 1, 1)]
void main(uint2 id : SV_GroupID)
{
    u[id] = t[id];
}
