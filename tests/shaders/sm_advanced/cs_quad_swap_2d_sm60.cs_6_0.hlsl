Texture2D<float> Tex : register(t0);
RWTexture2D<float4> SOut : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 thr : SV_DispatchThreadID)
{
        float v = Tex.Load(uint3(thr, 0));
        float horiz = QuadReadAcrossX(v);
        float vert = QuadReadAcrossY(v);
        float diag = QuadReadAcrossDiagonal(v);
        SOut[thr] = float4(v, horiz, vert, diag);
}