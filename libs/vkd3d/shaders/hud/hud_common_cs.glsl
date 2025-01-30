#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_vote : enable

/* For writing. This buffer is only used by the HUD update
 * and render shaders, so it doesn't need to be coherent. */
layout(buffer_reference, buffer_reference_align = 16, scalar)
buffer hud_buffer
{
    hud_font_info_t font;
    hud_text_buffer_out_t text;
    hud_stat_buffer_t stats;
};

struct string_state_t
{
    uint32_t shift;
};

/* Helper function to pre-populate a string and state with
 * characters from a single dword. */
void cs_string_init(inout u32vec4 str, inout string_state_t state, uint32_t chars)
{
    uint32_t size = (findMSB(chars) + 8u) / 8u;
    state.shift = -(8u * size);

    str.x = chars << (state.shift & 31u);
}

/* Helper function to prepend a char to a 16-byte in-register string */
void cs_string_prepend(inout u32vec4 str, inout string_state_t state, uint32_t ch)
{
    if ((state.shift & 31u) == 0u)
        str = u32vec4(0u, str.xyz);

    state.shift -= 8u;
    str.x |= ch << (state.shift & 31u);
}

/* Function to finish packing a string after adding characters to it */
void cs_string_finalize(inout u32vec4 str, string_state_t state)
{
    uint32_t shift = state.shift & 31u;

    if (shift != 0u)
    {
        str.x >>= shift;
        str.x |= str.y << (32u - shift);
        str.y >>= shift;
        str.y |= str.z << (32u - shift);
        str.z >>= shift;
        str.z |= str.w << (32u - shift);
        str.w >>= shift;
    }
}

/* Helper function to compute the string representation of a
 * number Supports positive or negative fixed-point values. */ 
void cs_string_from_int(inout u32vec4 str, inout string_state_t state, int32_t value, uint32_t decimal_point)
{
    uint32_t number = abs(value);

    if (decimal_point != 0u)
    {
        for (uint32_t i = 0u; i < decimal_point; i++)
        {
            /* Add ASCII '0' + current digit */
            cs_string_prepend(str, state, 0x30u + number % 10u);
            number /= 10u;
        }

        /* Add ASCII '.' */
        cs_string_prepend(str, state, 0x2eu);
    }

    do
    {
        /* Surely drivers optimize this, right? */
        cs_string_prepend(str, state, 0x30u + number % 10u);
    } while ((number /= 10u) != 0u);

    /* Add ASCII '-' for negative numbers */
    if (value < 0)
        cs_string_prepend(str, state, 0x2du);
}

/* Helper function to compute the size of a string */
uint32_t cs_string_length(in string_state_t state)
{
    return (-state.shift) / 8u;
}

/* Helper function to write out a string to the text buffer */
void cs_write_string(hud_buffer data, uint32_t draw_index, u32vec4 str, in string_state_t state)
{
    uint32_t offset = (data.text.draw_infos[draw_index].text_offset) / 16u;

    data.text.text_as_dquads[offset] = str;
    data.text.draw_commands[draw_index].vertex_count = 6u * cs_string_length(state);
}
