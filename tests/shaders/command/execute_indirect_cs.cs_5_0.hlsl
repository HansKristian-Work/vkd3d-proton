RWByteAddressBuffer o;

[numthreads(1, 1, 1)]
void main(uint3 group_id : SV_groupID)
{
    uint idx = group_id.x + group_id.y * 2 + group_id.z * 6;
    o.Store(idx * 4, idx);
}