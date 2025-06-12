RWTexture3D<uint> uav : register(u0);

cbuffer clear_args
{
        uint3 offset;
        uint value;
};

[numthreads(4, 4, 4)]
void main(uint3 coord : SV_DispatchThreadID)
{
        uav[offset + coord] = value;
}