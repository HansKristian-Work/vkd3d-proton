RWStructuredBuffer<uint> RWBuf : register(u0);
Buffer<uint> Buf : register(t0);

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        uint code;

        // Sample mapped, but freed memory. See what CheckAccessFullyMapped returns.
        uint data = Buf.Load(thr, code);
        uint value = CheckAccessFullyMapped(code) ? (1u << 16) : 0u;
        value |= data & 0xffffu;
        RWBuf[2 * thr + 0] = value;

        // Sample not yet mapped memory. See what CheckAccessFullyMapped returns.
        data = Buf.Load(thr + 1024 * 1024, code);
        value = CheckAccessFullyMapped(code) ? (1u << 16) : 0u;
        value |= data & 0xffffu;

        RWBuf[2 * thr + 1] = value;
}