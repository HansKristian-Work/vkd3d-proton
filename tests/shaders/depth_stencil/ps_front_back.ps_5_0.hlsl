float4 main(bool is_front_face : SV_IsFrontFace) : SV_TARGET
{
    return is_front_face
        ? float4(1.0f, 0.0f, 0.0f, 0.0f)
        : float4(0.0f, 1.0f, 0.0f, 0.0f);
}
