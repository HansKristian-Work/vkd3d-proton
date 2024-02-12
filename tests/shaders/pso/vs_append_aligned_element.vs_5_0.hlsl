struct vs_in
{
    float4 position : POSITION;
    float2 color_xy : COLOR0;
    float2 color_zw : COLOR1;
    unsigned int instance_id : SV_INSTANCEID;
};

struct vs_out
{
    float4 position : SV_POSITION;
    float2 color_xy : COLOR0;
    float2 color_zw : COLOR1;
};

struct vs_out main(struct vs_in i)
{
    struct vs_out o;

    o.position = i.position;
    o.position.x += i.instance_id * 0.5;
    o.color_xy = i.color_xy;
    o.color_zw = i.color_zw;

    return o;
}
