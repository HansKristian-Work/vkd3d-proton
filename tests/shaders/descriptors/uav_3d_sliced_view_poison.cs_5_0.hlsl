cbuffer C : register(b0) { uint value; }
RWTexture3D<uint> T : register(u0);

[numthreads(4, 4, 16)]
void main(uint3 thr : SV_DispatchThreadID)
{
        T[thr] = 0xdeadca7;
}