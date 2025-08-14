StructuredBuffer<uint4> Inputs;
RWStructuredBuffer<uint4> Outputs;

[numthreads(1, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
        Outputs[thr] = msad4(Inputs[thr].x, Inputs[thr].yz, Inputs[thr].wwww);
}
