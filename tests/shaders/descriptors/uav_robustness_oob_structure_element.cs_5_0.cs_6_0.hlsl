struct A { uint arr[4]; };
RWStructuredBuffer<A> U0 : register(u0);

cbuffer Cbuf : register(b0) { uint elem; uint index; uint value; };

[numthreads(1, 1, 1)]
void main()
{
    U0[elem].arr[index] = value;
}