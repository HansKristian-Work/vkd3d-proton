struct ps_data
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

float4 main(struct ps_data ps_input) : SV_Target
{
    return ps_input.color;
}