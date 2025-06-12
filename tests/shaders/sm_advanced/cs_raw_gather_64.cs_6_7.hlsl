Texture2D<uint64_t> D64 : register(t0);
SamplerState S : register(s0);
RWByteAddressBuffer U : register(u0);

[numthreads(1, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    U.Store<uint64_t4>(0, D64.GatherRaw(S, 0.5.xx));
    // GatherRed and friends are not legal on 64-bit.
}
