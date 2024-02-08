uint index;

cbuffer null_cb
{
    float4 data[1024];
};

float4 main() : SV_Target
{
    return data[index];
}