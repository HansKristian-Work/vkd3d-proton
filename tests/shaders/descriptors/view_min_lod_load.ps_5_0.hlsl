Texture2D tex;
float testLod;

float4 main() : SV_Target
{
    return tex.Load(int3(0, 0, int(testLod)));
}