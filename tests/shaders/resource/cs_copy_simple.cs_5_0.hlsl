Texture2D<float4> tex : register(t0);
RWTexture2D<float4> uav : register(u0);

[numthreads(4,4,1)]
void main(uint3 tid : SV_DISPATCHTHREADID)
{
    uav[tid.xy] = tex.Load(int3(tid.xy, 0));
}
