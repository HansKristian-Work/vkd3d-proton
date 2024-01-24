RWStructuredBuffer<uint> RW : register(u0);

[shader("raygeneration")]
void main()
{
        RW[0] = 0;
}