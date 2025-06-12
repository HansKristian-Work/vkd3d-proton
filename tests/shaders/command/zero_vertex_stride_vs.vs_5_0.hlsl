struct vs_data
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

void main(in struct vs_data vs_input, out struct vs_data vs_output)
{
    vs_output.pos = vs_input.pos;
    vs_output.color = vs_input.color;
}