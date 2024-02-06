Texture2D<uint> D32 : register(t0);
SamplerState S : register(s0);
RWByteAddressBuffer U : register(u0);

[numthreads(1, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    U.Store<uint4>(0, D32.GatherRaw(S, 0.5.xx));
    U.Store<uint4>(16, D32.GatherRed(S, 0.5.xx));
    U.Store<uint4>(32, D32.GatherGreen(S, 0.5.xx));
    U.Store<uint4>(48, D32.GatherBlue(S, 0.5.xx));
    U.Store<uint4>(64, D32.GatherAlpha(S, 0.5.xx));
}
