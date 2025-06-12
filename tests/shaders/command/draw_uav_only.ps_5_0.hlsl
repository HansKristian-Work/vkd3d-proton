RWTexture2D<int> u;

void main()
{
    InterlockedAdd(u[uint2(0, 0)], 1);
}