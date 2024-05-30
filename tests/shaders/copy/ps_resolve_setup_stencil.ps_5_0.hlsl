cbuffer sample_mask_t : register(b0)
{
    uint sample_mask;
};

uint main() : SV_Coverage
{
    return sample_mask;
}
