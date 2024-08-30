RWStructuredBuffer<uint> RWBuf : register(u1);
float4 main() : SV_Target
{
        uint v;
        InterlockedAdd(RWBuf[0], 100, v);
        return 1.0.xxxx;
}