Texture2D<uint16_t> D16 : register(t0);
SamplerState S : register(s0);
RWByteAddressBuffer U : register(u0);

[numthreads(1, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    U.Store<uint16_t4>(0, D16.GatherRaw(S, 0.5.xx));
    U.Store<uint16_t4>(8, D16.GatherRed(S, 0.5.xx));
    U.Store<uint16_t4>(16, D16.GatherGreen(S, 0.5.xx));
    U.Store<uint16_t4>(24, D16.GatherBlue(S, 0.5.xx));
    U.Store<uint16_t4>(32, D16.GatherAlpha(S, 0.5.xx));
}
