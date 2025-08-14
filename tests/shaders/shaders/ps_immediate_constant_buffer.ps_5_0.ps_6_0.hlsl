uint index;

static const int int_array[6] =
{
    310, 111, 212, -513, -318, 0,
};

static const uint uint_array[6] =
{
    2, 7, 0x7f800000, 0xff800000, 0x7fc00000, 0
};

static const float float_array[6] =
{
    76, 83.5f, 0.5f, 0.75f, -0.5f, 0.0f,
};

float4 main() : SV_Target
{
    return float4(int_array[index], uint_array[index], float_array[index], 1.0f);
}
