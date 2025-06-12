struct Payload
{
    float4 color;
};

[numthreads(1,1,1)]
void main()
{
    Payload payload = { float4(0.0f, 1.0f, 0.0f, 1.0f) };
    DispatchMesh(1, 1, 1, payload);
}
