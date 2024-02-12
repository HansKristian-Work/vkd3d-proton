struct vertex
{
    float4 position : SV_Position;
    float2 t0 : TEXCOORD0;
    nointerpolation float t1 : TEXCOORD1;
    uint t2 : TEXCOORD2;
    uint t3 : TEXCOORD3;
    float t4 : TEXCOORD4;
};

void main(in vertex vin, out vertex vout)
{
    vout = vin;
}
