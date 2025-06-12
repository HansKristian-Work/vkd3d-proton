cbuffer C : register(b0) { uint value; }
RWTexture3D<uint> T : register(u0);

[numthreads(4, 4, 16)]
void main(uint3 thr : SV_DispatchThreadID)
{
    uint w, h, d;
    T.GetDimensions(w, h, d);
    if (thr.z < d)
        T[thr] = value | (w << 8) | (h << 16) | (d << 24);
}