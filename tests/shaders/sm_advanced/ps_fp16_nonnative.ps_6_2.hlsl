struct VOut
{
    float4 pos : SV_Position;
    min16float2 v : V;
};

StructuredBuffer<min16float4> A : register(t0);
RWStructuredBuffer<min16float4> B : register(u0);

min16float4 main(VOut vout) : SV_Target
{
        min16float4 A0 = A[0];
        min16float4 A1 = A[1];
        B[0] = A0 + A1;
        B[1] = A0 - A1;
        return vout.v.xyxy * min16float(256.0 / 255.0);
}
