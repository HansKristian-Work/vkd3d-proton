cbuffer Cbuf : register(b0) { uint index; };

[numthreads(1, 1, 1)]
void main()
{
        RWStructuredBuffer<uint> Buf = ResourceDescriptorHeap[index];
        uint o;
        InterlockedAdd(Buf[0], 3, o);
        Buf.IncrementCounter();
}