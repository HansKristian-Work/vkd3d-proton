RWStructuredBuffer<uint> U0 : register(u0);
RWStructuredBuffer<uint> U1 : register(u1);
RWStructuredBuffer<uint> U2 : register(u2);
RWStructuredBuffer<uint> U3 : register(u3);

[shader("raygeneration")]
void Entry1()
{
        U0[0] = 0;
        U3[0] = 0;
}

[shader("raygeneration")]
void Entry2()
{
        U1[0] = 0;
        U3[0] = 0;
}

[shader("raygeneration")]
void Entry3()
{
        U2[0] = 0;
        U1[0] = 0;
}

[shader("raygeneration")]
void Entry4()
{
        U3[0] = 0;
        U1[0] = 0;
}