/*
 * Copyright 2008-2009 Henri Verbeet for CodeWeavers
 * Copyright 2017 Józef Kucia for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_SHADER

#include "vkd3d_shader_private.h"

#define MAKE_TAG(ch0, ch1, ch2, ch3) \
    ((DWORD)(ch0) | ((DWORD)(ch1) << 8) | \
    ((DWORD)(ch2) << 16) | ((DWORD)(ch3) << 24 ))
#define TAG_DXBC MAKE_TAG('D', 'X', 'B', 'C')
#define TAG_ISGN MAKE_TAG('I', 'S', 'G', 'N')
#define TAG_ISG1 MAKE_TAG('I', 'S', 'G', '1')
#define TAG_OSGN MAKE_TAG('O', 'S', 'G', 'N')
#define TAG_OSG5 MAKE_TAG('O', 'S', 'G', '5')
#define TAG_OSG1 MAKE_TAG('O', 'S', 'G', '1')
#define TAG_PCSG MAKE_TAG('P', 'C', 'S', 'G')
#define TAG_PSG1 MAKE_TAG('P', 'S', 'G', '1')
#define TAG_SHDR MAKE_TAG('S', 'H', 'D', 'R')
#define TAG_SHEX MAKE_TAG('S', 'H', 'E', 'X')
#define TAG_AON9 MAKE_TAG('A', 'o', 'n', '9')
#define TAG_RTS0 MAKE_TAG('R', 'T', 'S', '0')
#define TAG_DXIL MAKE_TAG('D', 'X', 'I', 'L')

static bool require_space(size_t offset, size_t count, size_t size, size_t data_size)
{
    return !count || (data_size - offset) / count >= size;
}

static void read_dword(const char **ptr, DWORD *d)
{
    memcpy(d, *ptr, sizeof(*d));
    *ptr += sizeof(*d);
}

static void read_uint32_(const char **ptr, void *u)
{
    memcpy(u, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
}

#define read_uint32(ptr, u) do { STATIC_ASSERT(sizeof(*(u)) == sizeof(uint32_t)); read_uint32_(ptr, u); } while(0)

static void read_float(const char **ptr, float *f)
{
    STATIC_ASSERT(sizeof(float) == sizeof(DWORD));
    read_dword(ptr, (DWORD *)f);
}

static void skip_dword_unknown(const char **ptr, unsigned int count)
{
    unsigned int i;
    DWORD d;

    WARN("Skipping %u unknown DWORDs:\n", count);
    for (i = 0; i < count; ++i)
    {
        read_dword(ptr, &d);
        WARN("\t0x%08x\n", d);
    }
}

static const char *shader_get_string(const char *data, size_t data_size, DWORD offset)
{
    size_t len, max_len;

    if (offset >= data_size)
    {
        WARN("Invalid offset %#x (data size %#lx).\n", offset, (long)data_size);
        return NULL;
    }

    max_len = data_size - offset;
    len = strnlen(data + offset, max_len);

    if (len == max_len)
        return NULL;

    return data + offset;
}

static int parse_dxbc(const char *data, size_t data_size,
        int (*chunk_handler)(const char *data, DWORD data_size, DWORD tag, void *ctx), void *ctx)
{
    const char *ptr = data;
    int ret = VKD3D_OK;
    DWORD chunk_count;
    DWORD total_size;
    unsigned int i;
    DWORD version;
    DWORD tag;

    if (data_size < VKD3D_DXBC_HEADER_SIZE)
    {
        WARN("Invalid data size %zu.\n", data_size);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    read_dword(&ptr, &tag);
    TRACE("tag: %#x.\n", tag);

    if (tag != TAG_DXBC)
    {
        WARN("Wrong tag.\n");
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    WARN("Ignoring DXBC checksum.\n");
    skip_dword_unknown(&ptr, 4);

    read_dword(&ptr, &version);
    TRACE("version: %#x.\n", version);
    if (version != 0x00000001)
    {
        WARN("Got unexpected DXBC version %#x.\n", version);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    read_dword(&ptr, &total_size);
    TRACE("total size: %#x\n", total_size);

    read_dword(&ptr, &chunk_count);
    TRACE("chunk count: %#x\n", chunk_count);

    for (i = 0; i < chunk_count; ++i)
    {
        DWORD chunk_tag, chunk_size;
        const char *chunk_ptr;
        DWORD chunk_offset;

        read_dword(&ptr, &chunk_offset);
        TRACE("chunk %u at offset %#x\n", i, chunk_offset);

        if (chunk_offset >= data_size || !require_space(chunk_offset, 2, sizeof(DWORD), data_size))
        {
            WARN("Invalid chunk offset %#x (data size %zu).\n", chunk_offset, data_size);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        chunk_ptr = data + chunk_offset;

        read_dword(&chunk_ptr, &chunk_tag);
        read_dword(&chunk_ptr, &chunk_size);

        if (!require_space(chunk_ptr - data, 1, chunk_size, data_size))
        {
            WARN("Invalid chunk size %#x (data size %zu, chunk offset %#x).\n",
                    chunk_size, data_size, chunk_offset);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        if ((ret = chunk_handler(chunk_ptr, chunk_size, chunk_tag, ctx)) < 0)
            break;
    }

    return ret;
}

static int shader_parse_signature(DWORD tag, const char *data, DWORD data_size,
        struct vkd3d_shader_signature *s)
{
    bool has_stream_index, has_min_precision;
    struct vkd3d_shader_signature_element *e;
    const char *ptr = data;
    unsigned int i;
    DWORD count;

    if (!require_space(0, 2, sizeof(DWORD), data_size))
    {
        WARN("Invalid data size %#x.\n", data_size);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    read_dword(&ptr, &count);
    TRACE("%u elements.\n", count);

    skip_dword_unknown(&ptr, 1); /* It seems to always be 0x00000008. */

    if (!require_space(ptr - data, count, 6 * sizeof(DWORD), data_size))
    {
        WARN("Invalid count %#x (data size %#x).\n", count, data_size);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if (!(e = vkd3d_calloc(count, sizeof(*e))))
    {
        ERR("Failed to allocate input signature memory.\n");
        return VKD3D_ERROR_OUT_OF_MEMORY;
    }

    has_min_precision = tag == TAG_OSG1 || tag == TAG_PSG1 || tag == TAG_ISG1;
    has_stream_index = tag == TAG_OSG5 || has_min_precision;

    for (i = 0; i < count; ++i)
    {
        DWORD name_offset;

        if (has_stream_index)
            read_uint32(&ptr, &e[i].stream_index);
        else
            e[i].stream_index = 0;

        read_dword(&ptr, &name_offset);
        if (!(e[i].semantic_name = shader_get_string(data, data_size, name_offset)))
        {
            WARN("Invalid name offset %#x (data size %#x).\n", name_offset, data_size);
            vkd3d_free(e);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }
        read_uint32(&ptr, &e[i].semantic_index);
        read_uint32(&ptr, &e[i].sysval_semantic);
        read_uint32(&ptr, &e[i].component_type);
        read_uint32(&ptr, &e[i].register_index);
        read_uint32(&ptr, &e[i].mask);

        if (has_min_precision)
            read_uint32(&ptr, &e[i].min_precision);
        else
            e[i].min_precision = VKD3D_SHADER_MINIMUM_PRECISION_NONE;

        TRACE("Stream: %u, semantic: %s, semantic idx: %u, sysval_semantic %#x, "
                "type %u, register idx: %u, use_mask %#x, input_mask %#x, precision %u.\n",
                e[i].stream_index, debugstr_a(e[i].semantic_name), e[i].semantic_index, e[i].sysval_semantic,
                e[i].component_type, e[i].register_index, (e[i].mask >> 8) & 0xff, e[i].mask & 0xff, e[i].min_precision);
    }

    s->elements = e;
    s->element_count = count;

    return VKD3D_OK;
}

static int isgn_handler(const char *data, DWORD data_size, DWORD tag, void *ctx)
{
    struct vkd3d_shader_signature *is = ctx;

    if (tag != TAG_ISGN && tag != TAG_ISG1)
        return VKD3D_OK;

    if (is->elements)
    {
        FIXME("Multiple input signatures.\n");
        vkd3d_shader_free_shader_signature(is);
    }
    return shader_parse_signature(tag, data, data_size, is);
}

static int osgn_handler(const char *data, DWORD data_size, DWORD tag, void *ctx)
{
    struct vkd3d_shader_signature *is = ctx;

    if (tag != TAG_OSGN && tag != TAG_OSG1 && tag != TAG_OSG5)
        return VKD3D_OK;

    if (is->elements)
    {
        FIXME("Multiple output signatures.\n");
        vkd3d_shader_free_shader_signature(is);
    }
    return shader_parse_signature(tag, data, data_size, is);
}

static int psgn_handler(const char *data, DWORD data_size, DWORD tag, void *ctx)
{
    struct vkd3d_shader_signature *sig = ctx;

    if (tag != TAG_PCSG && tag != TAG_PSG1)
        return VKD3D_OK;

    if (sig->elements)
    {
        FIXME("Multiple patch constant signatures.\n");
        vkd3d_shader_free_shader_signature(sig);
    }
    return shader_parse_signature(tag, data, data_size, sig);
}

int shader_parse_input_signature(const void *dxbc, size_t dxbc_length,
        struct vkd3d_shader_signature *signature)
{
    int ret;

    memset(signature, 0, sizeof(*signature));
    if ((ret = parse_dxbc(dxbc, dxbc_length, isgn_handler, signature)) < 0)
        ERR("Failed to parse input signature.\n");
    return ret;
}

int shader_parse_output_signature(const void *dxbc, size_t dxbc_length,
        struct vkd3d_shader_signature *signature)
{
    int ret;

    memset(signature, 0, sizeof(*signature));
    if ((ret = parse_dxbc(dxbc, dxbc_length, osgn_handler, signature)) < 0)
        ERR("Failed to parse output signature.\n");
    return ret;
}

int shader_parse_patch_constant_signature(const void *dxbc, size_t dxbc_length,
        struct vkd3d_shader_signature *signature)
{
    int ret;

    memset(signature, 0, sizeof(*signature));
    if ((ret = parse_dxbc(dxbc, dxbc_length, psgn_handler, signature)) < 0)
        ERR("Failed to parse output signature.\n");
    return ret;
}

static int dxil_handler(const char *data, DWORD data_size, DWORD tag, void *context)
{
    switch (tag)
    {
        case TAG_DXIL:
            *(bool *)context = true;
            break;
        default:
            break;
    }

    return VKD3D_OK;
}

bool shader_is_dxil(const void *dxbc, size_t dxbc_length)
{
    bool dxil = false;
    int ret = parse_dxbc(dxbc, dxbc_length, dxil_handler, &dxil);
    if (ret < 0)
    {
        FIXME("Failed to parse shader, vkd3d result %d.\n", ret);
        return false;
    }
    return dxil;
}

/* root signatures */
#define VKD3D_ROOT_SIGNATURE_1_0_ROOT_DESCRIPTOR_FLAGS VKD3D_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE

#define VKD3D_ROOT_SIGNATURE_1_0_DESCRIPTOR_RANGE_FLAGS \
        (VKD3D_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | VKD3D_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE)

struct root_signature_parser_context
{
    const char *data;
    unsigned int data_size;
};

static int shader_parse_descriptor_ranges(struct root_signature_parser_context *context,
        unsigned int offset, unsigned int count, struct vkd3d_descriptor_range *ranges)
{
    const char *ptr;
    unsigned int i;

    if (!require_space(offset, 5 * count, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u, count %u).\n", context->data_size, offset, count);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    for (i = 0; i < count; ++i)
    {
        read_uint32(&ptr, &ranges[i].range_type);
        read_uint32(&ptr, &ranges[i].descriptor_count);
        read_uint32(&ptr, &ranges[i].base_shader_register);
        read_uint32(&ptr, &ranges[i].register_space);
        read_uint32(&ptr, &ranges[i].descriptor_table_offset);

        TRACE("Type %#x, descriptor count %u, base shader register %u, "
                "register space %u, offset %u.\n",
                ranges[i].range_type, ranges[i].descriptor_count,
                ranges[i].base_shader_register, ranges[i].register_space,
                ranges[i].descriptor_table_offset);
    }

    return VKD3D_OK;
}

static void shader_validate_descriptor_range1(const struct vkd3d_descriptor_range1 *range)
{
    unsigned int unknown_flags = range->flags & ~(VKD3D_DESCRIPTOR_RANGE_FLAG_NONE
            | VKD3D_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
            | VKD3D_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE
            | VKD3D_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
            | VKD3D_DESCRIPTOR_RANGE_FLAG_DATA_STATIC
            | VKD3D_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS);

    if (unknown_flags)
        FIXME("Unknown descriptor range flags %#x.\n", unknown_flags);
}

static int shader_parse_descriptor_ranges1(struct root_signature_parser_context *context,
        unsigned int offset, unsigned int count, struct vkd3d_descriptor_range1 *ranges)
{
    const char *ptr;
    unsigned int i;

    if (!require_space(offset, 6 * count, sizeof(uint32_t), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u, count %u).\n", context->data_size, offset, count);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    for (i = 0; i < count; ++i)
    {
        read_uint32(&ptr, &ranges[i].range_type);
        read_uint32(&ptr, &ranges[i].descriptor_count);
        read_uint32(&ptr, &ranges[i].base_shader_register);
        read_uint32(&ptr, &ranges[i].register_space);
        read_uint32(&ptr, &ranges[i].flags);
        read_uint32(&ptr, &ranges[i].descriptor_table_offset);

        TRACE("Type %#x, descriptor count %u, base shader register %u, "
                "register space %u, flags %#x, offset %u.\n",
                ranges[i].range_type, ranges[i].descriptor_count,
                ranges[i].base_shader_register, ranges[i].register_space,
                ranges[i].flags, ranges[i].descriptor_table_offset);

        shader_validate_descriptor_range1(&ranges[i]);
    }

    return VKD3D_OK;
}

static int shader_parse_descriptor_table(struct root_signature_parser_context *context,
        unsigned int offset, struct vkd3d_root_descriptor_table *table)
{
    struct vkd3d_descriptor_range *ranges;
    unsigned int count;
    const char *ptr;

    if (!require_space(offset, 2, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u).\n", context->data_size, offset);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    read_uint32(&ptr, &count);
    read_uint32(&ptr, &offset);

    TRACE("Descriptor range count %u.\n", count);

    table->descriptor_range_count = count;

    if (!(ranges = vkd3d_calloc(count, sizeof(*ranges))))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    table->descriptor_ranges = ranges;
    return shader_parse_descriptor_ranges(context, offset, count, ranges);
}

static int shader_parse_descriptor_table1(struct root_signature_parser_context *context,
        unsigned int offset, struct vkd3d_root_descriptor_table1 *table)
{
    struct vkd3d_descriptor_range1 *ranges;
    unsigned int count;
    const char *ptr;

    if (!require_space(offset, 2, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u).\n", context->data_size, offset);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    read_uint32(&ptr, &count);
    read_uint32(&ptr, &offset);

    TRACE("Descriptor range count %u.\n", count);

    table->descriptor_range_count = count;

    if (!(ranges = vkd3d_calloc(count, sizeof(*ranges))))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    table->descriptor_ranges = ranges;
    return shader_parse_descriptor_ranges1(context, offset, count, ranges);
}

static int shader_parse_root_constants(struct root_signature_parser_context *context,
        unsigned int offset, struct vkd3d_root_constants *constants)
{
    const char *ptr;

    if (!require_space(offset, 3, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u).\n", context->data_size, offset);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    read_uint32(&ptr, &constants->shader_register);
    read_uint32(&ptr, &constants->register_space);
    read_uint32(&ptr, &constants->value_count);

    TRACE("Shader register %u, register space %u, 32-bit value count %u.\n",
            constants->shader_register, constants->register_space, constants->value_count);

    return VKD3D_OK;
}

static int shader_parse_root_descriptor(struct root_signature_parser_context *context,
        unsigned int offset, struct vkd3d_root_descriptor *descriptor)
{
    const char *ptr;

    if (!require_space(offset, 2, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u).\n", context->data_size, offset);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    read_uint32(&ptr, &descriptor->shader_register);
    read_uint32(&ptr, &descriptor->register_space);

    TRACE("Shader register %u, register space %u.\n",
            descriptor->shader_register, descriptor->register_space);

    return VKD3D_OK;
}

static void shader_validate_root_descriptor1(const struct vkd3d_root_descriptor1 *descriptor)
{
    unsigned int unknown_flags = descriptor->flags & ~(VKD3D_ROOT_DESCRIPTOR_FLAG_NONE
            | VKD3D_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE
            | VKD3D_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
            | VKD3D_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);

    if (unknown_flags)
        FIXME("Unknown root descriptor flags %#x.\n", unknown_flags);
}

static int shader_parse_root_descriptor1(struct root_signature_parser_context *context,
        unsigned int offset, struct vkd3d_root_descriptor1 *descriptor)
{
    const char *ptr;

    if (!require_space(offset, 3, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u).\n", context->data_size, offset);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    read_uint32(&ptr, &descriptor->shader_register);
    read_uint32(&ptr, &descriptor->register_space);
    read_uint32(&ptr, &descriptor->flags);

    TRACE("Shader register %u, register space %u, flags %#x.\n",
            descriptor->shader_register, descriptor->register_space, descriptor->flags);

    shader_validate_root_descriptor1(descriptor);

    return VKD3D_OK;
}

static int shader_parse_root_parameters(struct root_signature_parser_context *context,
        unsigned int offset, unsigned int count, struct vkd3d_root_parameter *parameters)
{
    const char *ptr;
    unsigned int i;
    int ret;

    if (!require_space(offset, 3 * count, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u, count %u).\n", context->data_size, offset, count);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    for (i = 0; i < count; ++i)
    {
        read_uint32(&ptr, &parameters[i].parameter_type);
        read_uint32(&ptr, &parameters[i].shader_visibility);
        read_uint32(&ptr, &offset);

        TRACE("Type %#x, shader visibility %#x.\n",
                parameters[i].parameter_type, parameters[i].shader_visibility);

        switch (parameters[i].parameter_type)
        {
            case VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                ret = shader_parse_descriptor_table(context, offset, &parameters[i].descriptor_table);
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                ret = shader_parse_root_constants(context, offset, &parameters[i].constants);
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_CBV:
            case VKD3D_ROOT_PARAMETER_TYPE_SRV:
            case VKD3D_ROOT_PARAMETER_TYPE_UAV:
                ret = shader_parse_root_descriptor(context, offset, &parameters[i].descriptor);
                break;
            default:
                FIXME("Unrecognized type %#x.\n", parameters[i].parameter_type);
                return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        if (ret < 0)
            return ret;
    }

    return VKD3D_OK;
}

static int shader_parse_root_parameters1(struct root_signature_parser_context *context,
        DWORD offset, DWORD count, struct vkd3d_root_parameter1 *parameters)
{
    const char *ptr;
    unsigned int i;
    int ret;

    if (!require_space(offset, 3 * count, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u, count %u).\n", context->data_size, offset, count);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    for (i = 0; i < count; ++i)
    {
        read_uint32(&ptr, &parameters[i].parameter_type);
        read_uint32(&ptr, &parameters[i].shader_visibility);
        read_dword(&ptr, &offset);

        TRACE("Type %#x, shader visibility %#x.\n",
                parameters[i].parameter_type, parameters[i].shader_visibility);

        switch (parameters[i].parameter_type)
        {
            case VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                ret = shader_parse_descriptor_table1(context, offset, &parameters[i].descriptor_table);
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                ret = shader_parse_root_constants(context, offset, &parameters[i].constants);
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_CBV:
            case VKD3D_ROOT_PARAMETER_TYPE_SRV:
            case VKD3D_ROOT_PARAMETER_TYPE_UAV:
                ret = shader_parse_root_descriptor1(context, offset, &parameters[i].descriptor);
                break;
            default:
                FIXME("Unrecognized type %#x.\n", parameters[i].parameter_type);
                return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        if (ret < 0)
            return ret;
    }

    return VKD3D_OK;
}

static void shader_parse_static_sampler_payload_base(const char **ptr,
        struct vkd3d_static_sampler_desc *sampler_desc)
{
    read_uint32(ptr, &sampler_desc->filter);
    read_uint32(ptr, &sampler_desc->address_u);
    read_uint32(ptr, &sampler_desc->address_v);
    read_uint32(ptr, &sampler_desc->address_w);
    read_float(ptr, &sampler_desc->mip_lod_bias);
    read_uint32(ptr, &sampler_desc->max_anisotropy);
    read_uint32(ptr, &sampler_desc->comparison_func);
    read_uint32(ptr, &sampler_desc->border_color);
    read_float(ptr, &sampler_desc->min_lod);
    read_float(ptr, &sampler_desc->max_lod);
    read_uint32(ptr, &sampler_desc->shader_register);
    read_uint32(ptr, &sampler_desc->register_space);
    read_uint32(ptr, &sampler_desc->shader_visibility);
}

static int shader_parse_static_samplers(struct root_signature_parser_context *context,
        unsigned int offset, unsigned int count, struct vkd3d_static_sampler_desc *sampler_descs)
{
    const char *ptr;
    unsigned int i;

    if (!require_space(offset, 13 * count, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u, count %u).\n", context->data_size, offset, count);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    for (i = 0; i < count; ++i)
        shader_parse_static_sampler_payload_base(&ptr, &sampler_descs[i]);

    return VKD3D_OK;
}

static int shader_parse_static_samplers1(struct root_signature_parser_context *context,
        unsigned int offset, unsigned int count, struct vkd3d_static_sampler_desc1 *sampler_descs)
{
    const char *ptr;
    unsigned int i;

    if (!require_space(offset, 14 * count, sizeof(DWORD), context->data_size))
    {
        WARN("Invalid data size %#x (offset %u, count %u).\n", context->data_size, offset, count);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    ptr = &context->data[offset];

    for (i = 0; i < count; ++i)
    {
        shader_parse_static_sampler_payload_base(&ptr, (struct vkd3d_static_sampler_desc *)&sampler_descs[i]);
        read_uint32(&ptr, &sampler_descs[i].flags);
    }

    return VKD3D_OK;
}

int vkd3d_shader_parse_root_signature_raw(const char *data, unsigned int data_size,
        struct vkd3d_versioned_root_signature_desc *desc,
        vkd3d_shader_hash_t *compatibility_hash)
{
    struct vkd3d_root_signature_desc2 *v_1_2 = &desc->v_1_2;
    struct vkd3d_root_signature_desc *v_1_0 = &desc->v_1_0;
    struct root_signature_parser_context context;
    unsigned int count, offset, version;
    const char *ptr = data;
    int ret;

    memset(desc, 0, sizeof(*desc));

    context.data = data;
    context.data_size = data_size;

    if (!require_space(0, 6, sizeof(DWORD), data_size))
    {
        WARN("Invalid data size %#x.\n", data_size);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    read_uint32(&ptr, &version);
    TRACE("Version %#x.\n", version);
    if (!vkd3d_root_signature_version_is_supported(version))
    {
        FIXME("Unknown version %#x.\n", version);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }
    desc->version = version;

    read_uint32(&ptr, &count);
    read_uint32(&ptr, &offset);
    TRACE("Parameter count %u, offset %u.\n", count, offset);

    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        v_1_0->parameter_count = count;
        if (v_1_0->parameter_count)
        {
            struct vkd3d_root_parameter *parameters;
            if (!(parameters = vkd3d_calloc(v_1_0->parameter_count, sizeof(*parameters))))
                return VKD3D_ERROR_OUT_OF_MEMORY;
            v_1_0->parameters = parameters;
            if ((ret = shader_parse_root_parameters(&context, offset, count, parameters)) < 0)
                return ret;
        }
    }
    else
    {
        struct vkd3d_root_signature_desc1 *v_1_1 = &desc->v_1_1;

        assert(version == VKD3D_ROOT_SIGNATURE_VERSION_1_1 || version == VKD3D_ROOT_SIGNATURE_VERSION_1_2);

        v_1_1->parameter_count = count;
        if (v_1_1->parameter_count)
        {
            struct vkd3d_root_parameter1 *parameters;
            if (!(parameters = vkd3d_calloc(v_1_1->parameter_count, sizeof(*parameters))))
                return VKD3D_ERROR_OUT_OF_MEMORY;
            v_1_1->parameters = parameters;
            if ((ret = shader_parse_root_parameters1(&context, offset, count, parameters)) < 0)
                return ret;
        }
    }

    read_uint32(&ptr, &count);
    read_uint32(&ptr, &offset);
    TRACE("Static sampler count %u, offset %u.\n", count, offset);

    if (version == VKD3D_ROOT_SIGNATURE_VERSION_1_2)
    {
        v_1_2->static_sampler_count = count;
        if (v_1_2->static_sampler_count)
        {
            struct vkd3d_static_sampler_desc1 *samplers;
            if (!(samplers = vkd3d_calloc(v_1_2->static_sampler_count, sizeof(*samplers))))
                return VKD3D_ERROR_OUT_OF_MEMORY;
            v_1_2->static_samplers = samplers;
            if ((ret = shader_parse_static_samplers1(&context, offset, count, samplers)) < 0)
                return ret;
        }
    }
    else
    {
        assert(version == VKD3D_ROOT_SIGNATURE_VERSION_1_0 || version == VKD3D_ROOT_SIGNATURE_VERSION_1_1);

        v_1_0->static_sampler_count = count;
        if (v_1_0->static_sampler_count)
        {
            struct vkd3d_static_sampler_desc *samplers;
            if (!(samplers = vkd3d_calloc(v_1_0->static_sampler_count, sizeof(*samplers))))
                return VKD3D_ERROR_OUT_OF_MEMORY;
            v_1_0->static_samplers = samplers;
            if ((ret = shader_parse_static_samplers(&context, offset, count, samplers)) < 0)
                return ret;
        }
    }

    read_uint32(&ptr, &v_1_0->flags);
    TRACE("Flags %#x.\n", v_1_0->flags);

    if (compatibility_hash)
    {
        struct vkd3d_shader_code code = { data, data_size };
        *compatibility_hash = vkd3d_shader_hash(&code);
        vkd3d_shader_dump_shader(*compatibility_hash, &code, "rs");
    }

    return VKD3D_OK;
}

static int rts0_handler(const char *data, DWORD data_size, DWORD tag, void *context)
{
    struct vkd3d_shader_code *payload = context;

    if (tag != TAG_RTS0)
        return VKD3D_OK;

    payload->code = data;
    payload->size = data_size;
    return VKD3D_OK;
}

bool vkd3d_shader_contains_root_signature(const void *code, size_t size)
{
    struct vkd3d_shader_code raw_payload;
    int ret;
    memset(&raw_payload, 0, sizeof(raw_payload));
    if ((ret = parse_dxbc(code, size, rts0_handler, &raw_payload)) < 0)
        return false;
    return raw_payload.size != 0;
}

int vkd3d_shader_parse_root_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *root_signature,
        vkd3d_shader_hash_t *compatibility_hash)
{
    struct vkd3d_shader_code raw_payload;
    int ret;

    TRACE("dxbc {%p, %zu}, root_signature %p.\n", dxbc->code, dxbc->size, root_signature);

    memset(&raw_payload, 0, sizeof(raw_payload));

    if ((ret = parse_dxbc(dxbc->code, dxbc->size, rts0_handler, &raw_payload)) < 0)
        return ret;

    if (!raw_payload.code)
    {
        /* This might be a DXIL lib target in which case we have to parse subobjects. */
        if (!shader_is_dxil(dxbc->code, dxbc->size))
            return VKD3D_ERROR;

        /* Payload subobjects do not own any memory, they point directly to blob. No need to free. */
        if ((ret = vkd3d_shader_dxil_find_global_root_signature_subobject(dxbc->code, dxbc->size, &raw_payload)))
            return ret;
    }

    if ((ret = vkd3d_shader_parse_root_signature_raw(raw_payload.code, raw_payload.size,
            root_signature, compatibility_hash)) < 0)
    {
        vkd3d_shader_free_root_signature(root_signature);
        return ret;
    }

    return VKD3D_OK;
}

static unsigned int versioned_root_signature_get_parameter_count(const struct vkd3d_versioned_root_signature_desc *desc)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
        return desc->v_1_0.parameter_count;
    else
        return desc->v_1_1.parameter_count;
}

static enum vkd3d_root_parameter_type versioned_root_signature_get_parameter_type(
        const struct vkd3d_versioned_root_signature_desc *desc, unsigned int i)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
        return desc->v_1_0.parameters[i].parameter_type;
    else
        return desc->v_1_1.parameters[i].parameter_type;
}

static enum vkd3d_shader_visibility versioned_root_signature_get_parameter_shader_visibility(
        const struct vkd3d_versioned_root_signature_desc *desc, unsigned int i)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
        return desc->v_1_0.parameters[i].shader_visibility;
    else
        return desc->v_1_1.parameters[i].shader_visibility;
}

static const struct vkd3d_root_constants *versioned_root_signature_get_root_constants(
        const struct vkd3d_versioned_root_signature_desc *desc, unsigned int i)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
        return &desc->v_1_0.parameters[i].constants;
    else
        return &desc->v_1_1.parameters[i].constants;
}

static unsigned int versioned_root_signature_get_static_sampler_count(const struct vkd3d_versioned_root_signature_desc *desc)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
        return desc->v_1_0.static_sampler_count;
    else
        return desc->v_1_1.static_sampler_count;
}

static const struct vkd3d_static_sampler_desc *versioned_root_signature_get_static_sampler(
        const struct vkd3d_versioned_root_signature_desc *desc, unsigned int index)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
        return &desc->v_1_0.static_samplers[index];
    else if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_1)
        return &desc->v_1_1.static_samplers[index];
    else
    {
        /* STATIC_SAMPLER1 just appends flags member. */
        return (const struct vkd3d_static_sampler_desc *)&desc->v_1_2.static_samplers[index];
    }
}

static const struct vkd3d_static_sampler_desc1 *versioned_root_signature_get_static_sampler1(
        const struct vkd3d_versioned_root_signature_desc *desc, unsigned int index)
{
    if (desc->version < VKD3D_ROOT_SIGNATURE_VERSION_1_2)
        return NULL;
    else
        return &desc->v_1_2.static_samplers[index];
}

static unsigned int versioned_root_signature_get_flags(const struct vkd3d_versioned_root_signature_desc *desc)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
        return desc->v_1_0.flags;
    else
        return desc->v_1_1.flags;
}

struct root_signature_writer_context
{
    DWORD *data;
    size_t position;
    size_t capacity;

    size_t total_size_position;
    size_t chunk_position;
};

static bool write_dwords(struct root_signature_writer_context *context,
        unsigned int count, DWORD d)
{
    unsigned int i;

    if (!vkd3d_array_reserve((void **)&context->data, &context->capacity,
            context->position + count, sizeof(*context->data)))
        return false;
    for (i = 0; i < count; ++i)
        context->data[context->position++] = d;
    return true;
}

static bool write_dword(struct root_signature_writer_context *context, DWORD d)
{
    return write_dwords(context, 1, d);
}

static bool write_float(struct root_signature_writer_context *context, float f)
{
    union
    {
        float f;
        DWORD d;
    } u;
    u.f = f;
    return write_dword(context, u.d);
}

static size_t get_chunk_offset(struct root_signature_writer_context *context)
{
    return (context->position - context->chunk_position) * sizeof(DWORD);
}

static int shader_write_root_signature_header(struct root_signature_writer_context *context)
{
    if (!write_dword(context, TAG_DXBC))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    /* The checksum is computed when all data is generated. */
    if (!write_dwords(context, 4, 0x00000000))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    if (!write_dword(context, 0x00000001))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    context->total_size_position = context->position;
    if (!write_dword(context, 0xffffffff)) /* total size */
        return VKD3D_ERROR_OUT_OF_MEMORY;

    if (!write_dword(context, 1)) /* chunk count */
        return VKD3D_ERROR_OUT_OF_MEMORY;

    /* chunk offset */
    if (!write_dword(context, (context->position + 1) * sizeof(DWORD)))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    if (!write_dword(context, TAG_RTS0))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, 0xffffffff)) /* chunk size */
        return VKD3D_ERROR_OUT_OF_MEMORY;
    context->chunk_position = context->position;

    return VKD3D_OK;
}

static int shader_write_descriptor_ranges(struct root_signature_writer_context *context,
        const struct vkd3d_root_descriptor_table *table)
{
    const struct vkd3d_descriptor_range *ranges = table->descriptor_ranges;
    unsigned int i;

    for (i = 0; i < table->descriptor_range_count; ++i)
    {
        if (!write_dword(context, ranges[i].range_type))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].descriptor_count))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].base_shader_register))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].register_space))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].descriptor_table_offset))
            return VKD3D_ERROR_OUT_OF_MEMORY;
    }

    return VKD3D_OK;
}

static int shader_write_descriptor_ranges1(struct root_signature_writer_context *context,
        const struct vkd3d_root_descriptor_table1 *table)
{
    const struct vkd3d_descriptor_range1 *ranges = table->descriptor_ranges;
    unsigned int i;

    for (i = 0; i < table->descriptor_range_count; ++i)
    {
        if (!write_dword(context, ranges[i].range_type))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].descriptor_count))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].base_shader_register))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].register_space))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].flags))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, ranges[i].descriptor_table_offset))
            return VKD3D_ERROR_OUT_OF_MEMORY;
    }

    return VKD3D_OK;
}

static int shader_write_descriptor_table(struct root_signature_writer_context *context,
        const struct vkd3d_root_descriptor_table *table)
{
    if (!write_dword(context, table->descriptor_range_count))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, get_chunk_offset(context) + sizeof(DWORD))) /* offset */
        return VKD3D_ERROR_OUT_OF_MEMORY;

    return shader_write_descriptor_ranges(context, table);
}

static int shader_write_descriptor_table1(struct root_signature_writer_context *context,
        const struct vkd3d_root_descriptor_table1 *table)
{
    if (!write_dword(context, table->descriptor_range_count))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, get_chunk_offset(context) + sizeof(DWORD))) /* offset */
        return VKD3D_ERROR_OUT_OF_MEMORY;

    return shader_write_descriptor_ranges1(context, table);
}

static int shader_write_root_constants(struct root_signature_writer_context *context,
        const struct vkd3d_root_constants *constants)
{
    if (!write_dword(context, constants->shader_register))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, constants->register_space))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, constants->value_count))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    return VKD3D_OK;
}

static int shader_write_root_descriptor(struct root_signature_writer_context *context,
        const struct vkd3d_root_descriptor *descriptor)
{
    if (!write_dword(context, descriptor->shader_register))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, descriptor->register_space))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    return VKD3D_OK;
}

static int shader_write_root_descriptor1(struct root_signature_writer_context *context,
        const struct vkd3d_root_descriptor1 *descriptor)
{
    if (!write_dword(context, descriptor->shader_register))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, descriptor->register_space))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, descriptor->flags))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    return VKD3D_OK;
}

static int shader_write_root_parameters(struct root_signature_writer_context *context,
        const struct vkd3d_versioned_root_signature_desc *desc)
{
    unsigned int parameter_count = versioned_root_signature_get_parameter_count(desc);
    size_t parameters_position;
    unsigned int i;
    int ret;

    parameters_position = context->position;
    for (i = 0; i < parameter_count; ++i)
    {
        if (!write_dword(context, versioned_root_signature_get_parameter_type(desc, i)))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, versioned_root_signature_get_parameter_shader_visibility(desc, i)))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, 0xffffffff)) /* offset */
            return VKD3D_ERROR_OUT_OF_MEMORY;
    }

    for (i = 0; i < parameter_count; ++i)
    {
        context->data[parameters_position + 3 * i + 2] = get_chunk_offset(context); /* offset */

        switch (versioned_root_signature_get_parameter_type(desc, i))
        {
            case VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
                    ret = shader_write_descriptor_table(context, &desc->v_1_0.parameters[i].descriptor_table);
                else
                    ret = shader_write_descriptor_table1(context, &desc->v_1_1.parameters[i].descriptor_table);
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                ret = shader_write_root_constants(context, versioned_root_signature_get_root_constants(desc, i));
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_CBV:
            case VKD3D_ROOT_PARAMETER_TYPE_SRV:
            case VKD3D_ROOT_PARAMETER_TYPE_UAV:
                if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
                    ret = shader_write_root_descriptor(context, &desc->v_1_0.parameters[i].descriptor);
                else
                    ret = shader_write_root_descriptor1(context, &desc->v_1_1.parameters[i].descriptor);
                break;
            default:
                FIXME("Unrecognized type %#x.\n", versioned_root_signature_get_parameter_type(desc, i));
                return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        if (ret < 0)
            return ret;
    }

    return VKD3D_OK;
}

static int shader_write_static_samplers(struct root_signature_writer_context *context,
        const struct vkd3d_versioned_root_signature_desc *desc)
{
    const struct vkd3d_static_sampler_desc1 *sampler1;
    const struct vkd3d_static_sampler_desc *sampler;
    unsigned int i;

    for (i = 0; i < versioned_root_signature_get_static_sampler_count(desc); ++i)
    {
        sampler = versioned_root_signature_get_static_sampler(desc, i);
        if (!write_dword(context, sampler->filter))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->address_u))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->address_v))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->address_w))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_float(context, sampler->mip_lod_bias))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->max_anisotropy))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->comparison_func))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->border_color))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_float(context, sampler->min_lod))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_float(context, sampler->max_lod))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->shader_register))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->register_space))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (!write_dword(context, sampler->shader_visibility))
            return VKD3D_ERROR_OUT_OF_MEMORY;
        if (desc->version >= VKD3D_ROOT_SIGNATURE_VERSION_1_2)
        {
            sampler1 = versioned_root_signature_get_static_sampler1(desc, i);
            if (!write_dword(context, sampler1->flags))
                return VKD3D_ERROR_OUT_OF_MEMORY;
        }
    }

    return VKD3D_OK;
}

static int shader_write_root_signature(struct root_signature_writer_context *context,
        const struct vkd3d_versioned_root_signature_desc *desc)
{
    size_t samplers_offset_position;
    int ret;

    if (!write_dword(context, desc->version))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    if (!write_dword(context, versioned_root_signature_get_parameter_count(desc)))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    if (!write_dword(context, get_chunk_offset(context) + 4 * sizeof(DWORD))) /* offset */
        return VKD3D_ERROR_OUT_OF_MEMORY;

    if (!write_dword(context, versioned_root_signature_get_static_sampler_count(desc)))
        return VKD3D_ERROR_OUT_OF_MEMORY;
    samplers_offset_position = context->position;
    if (!write_dword(context, 0xffffffff)) /* offset */
        return VKD3D_ERROR_OUT_OF_MEMORY;

    if (!write_dword(context, versioned_root_signature_get_flags(desc)))
        return VKD3D_ERROR_OUT_OF_MEMORY;

    if ((ret = shader_write_root_parameters(context, desc)) < 0)
        return ret;

    context->data[samplers_offset_position] = get_chunk_offset(context);
    return shader_write_static_samplers(context, desc);
}

static int validate_descriptor_table_v_1_0(const struct vkd3d_root_descriptor_table *descriptor_table)
{
    bool have_srv_uav_cbv = false;
    bool have_sampler = false;
    unsigned int i;

    for (i = 0; i < descriptor_table->descriptor_range_count; ++i)
    {
        const struct vkd3d_descriptor_range *r = &descriptor_table->descriptor_ranges[i];

        if (r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_SRV
                || r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_UAV
                || r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_CBV)
        {
            have_srv_uav_cbv = true;
        }
        else if (r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_SAMPLER)
        {
            have_sampler = true;
        }
        else
        {
            WARN("Invalid descriptor range type %#x.\n", r->range_type);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }
    }

    if (have_srv_uav_cbv && have_sampler)
    {
        WARN("Samplers cannot be mixed with CBVs/SRVs/UAVs in descriptor tables.\n");
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    return VKD3D_OK;
}

static int validate_descriptor_table_v_1_1(const struct vkd3d_root_descriptor_table1 *descriptor_table)
{
    bool have_srv_uav_cbv = false;
    bool have_sampler = false;
    unsigned int i;

    for (i = 0; i < descriptor_table->descriptor_range_count; ++i)
    {
        const struct vkd3d_descriptor_range1 *r = &descriptor_table->descriptor_ranges[i];

        if (r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_SRV
                || r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_UAV
                || r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_CBV)
        {
            have_srv_uav_cbv = true;
        }
        else if (r->range_type == VKD3D_DESCRIPTOR_RANGE_TYPE_SAMPLER)
        {
            have_sampler = true;
        }
        else
        {
            WARN("Invalid descriptor range type %#x.\n", r->range_type);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }
    }

    if (have_srv_uav_cbv && have_sampler)
    {
        WARN("Samplers cannot be mixed with CBVs/SRVs/UAVs in descriptor tables.\n");
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    return VKD3D_OK;
}

static int validate_root_signature_desc(const struct vkd3d_versioned_root_signature_desc *desc)
{
    int ret = VKD3D_OK;
    unsigned int i;

    for (i = 0; i < versioned_root_signature_get_parameter_count(desc); ++i)
    {
        enum vkd3d_root_parameter_type type;

        type = versioned_root_signature_get_parameter_type(desc, i);
        if (type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
                ret = validate_descriptor_table_v_1_0(&desc->v_1_0.parameters[i].descriptor_table);
            else
                ret = validate_descriptor_table_v_1_1(&desc->v_1_1.parameters[i].descriptor_table);
        }

        if (ret < 0)
            break;
    }

    return ret;
}

int vkd3d_shader_serialize_root_signature(const struct vkd3d_versioned_root_signature_desc *root_signature,
        struct vkd3d_shader_code *dxbc)
{
    struct root_signature_writer_context context;
    size_t total_size, chunk_size;
    uint32_t checksum[4];
    int ret;

    TRACE("root_signature %p, dxbc %p.\n", root_signature, dxbc);

    if (!vkd3d_root_signature_version_is_supported(root_signature->version))
    {
        WARN("Root signature version %#x not supported.\n", root_signature->version);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if ((ret = validate_root_signature_desc(root_signature)) < 0)
        return ret;

    memset(dxbc, 0, sizeof(*dxbc));
    memset(&context, 0, sizeof(context));
    if ((ret = shader_write_root_signature_header(&context)) < 0)
    {
        vkd3d_free(context.data);
        return ret;
    }

    if ((ret = shader_write_root_signature(&context, root_signature)) < 0)
    {
        vkd3d_free(context.data);
        return ret;
    }

    total_size = context.position * sizeof(DWORD);
    chunk_size = get_chunk_offset(&context);
    context.data[context.total_size_position] = total_size;
    context.data[context.chunk_position - 1] = chunk_size;

    dxbc->code = context.data;
    dxbc->size = total_size;

    vkd3d_compute_dxbc_checksum(dxbc->code, dxbc->size, checksum);
    memcpy((uint32_t *)dxbc->code + 1, checksum, sizeof(checksum));

    return VKD3D_OK;
}

static void free_descriptor_ranges(const struct vkd3d_root_parameter *parameters, unsigned int count)
{
    unsigned int i;

    if (!parameters)
        return;

    for (i = 0; i < count; ++i)
    {
        const struct vkd3d_root_parameter *p = &parameters[i];

        if (p->parameter_type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)p->descriptor_table.descriptor_ranges);
    }
}

static int convert_root_parameters_to_v_1_0(struct vkd3d_root_parameter *dst,
        const struct vkd3d_root_parameter1 *src, unsigned int count)
{
    const struct vkd3d_descriptor_range1 *ranges1;
    struct vkd3d_descriptor_range *ranges;
    unsigned int i, j;
    int ret;

    for (i = 0; i < count; ++i)
    {
        const struct vkd3d_root_parameter1 *p1 = &src[i];
        struct vkd3d_root_parameter *p = &dst[i];

        p->parameter_type = p1->parameter_type;
        switch (p->parameter_type)
        {
            case VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                ranges = NULL;
                if ((p->descriptor_table.descriptor_range_count = p1->descriptor_table.descriptor_range_count))
                {
                    if (!(ranges = vkd3d_calloc(p->descriptor_table.descriptor_range_count, sizeof(*ranges))))
                    {
                        ret = VKD3D_ERROR_OUT_OF_MEMORY;
                        goto fail;
                    }
                }
                p->descriptor_table.descriptor_ranges = ranges;
                ranges1 = p1->descriptor_table.descriptor_ranges;
                for (j = 0; j < p->descriptor_table.descriptor_range_count; ++j)
                {
                    ranges[j].range_type = ranges1[j].range_type;
                    ranges[j].descriptor_count = ranges1[j].descriptor_count;
                    ranges[j].base_shader_register = ranges1[j].base_shader_register;
                    ranges[j].register_space = ranges1[j].register_space;
                    ranges[j].descriptor_table_offset = ranges1[j].descriptor_table_offset;
                }
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                p->constants = p1->constants;
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_CBV:
            case VKD3D_ROOT_PARAMETER_TYPE_SRV:
            case VKD3D_ROOT_PARAMETER_TYPE_UAV:
                p->descriptor.shader_register = p1->descriptor.shader_register;
                p->descriptor.register_space = p1->descriptor.register_space;
                break;
            default:
                WARN("Invalid root parameter type %#x.\n", p->parameter_type);
                ret = VKD3D_ERROR_INVALID_ARGUMENT;
                goto fail;

        }
        p->shader_visibility = p1->shader_visibility;
    }

    return VKD3D_OK;

fail:
    free_descriptor_ranges(dst, i);
    return ret;
}

static void convert_static_sampler_to_v_1_0(struct vkd3d_static_sampler_desc *dst,
        const struct vkd3d_static_sampler_desc1 *src)
{
    /* Just drop the flags field. This is not an error in the runtime. */
    memcpy(dst, src, sizeof(*dst));
}

static void convert_static_sampler_to_v_1_2(struct vkd3d_static_sampler_desc1 *dst,
        const struct vkd3d_static_sampler_desc *src)
{
    memcpy(dst, src, sizeof(*src));
    dst->flags = VKD3D_SAMPLER_FLAG_NONE;
}

static int convert_root_signature_to_v1_0(struct vkd3d_versioned_root_signature_desc *dst,
        const struct vkd3d_versioned_root_signature_desc *src)
{
    const struct vkd3d_root_signature_desc2 *src_desc2 = &src->v_1_2;
    const struct vkd3d_root_signature_desc1 *src_desc = &src->v_1_1;
    struct vkd3d_root_signature_desc *dst_desc = &dst->v_1_0;
    struct vkd3d_static_sampler_desc *samplers = NULL;
    struct vkd3d_root_parameter *parameters = NULL;
    unsigned int i;
    int ret;

    /* v1.1 and v1.2 are identical for root parameters. */
    assert(src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_1 || src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_2);

    if ((dst_desc->parameter_count = src_desc->parameter_count))
    {
        if (!(parameters = vkd3d_calloc(dst_desc->parameter_count, sizeof(*parameters))))
        {
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
            goto fail;
        }
        if ((ret = convert_root_parameters_to_v_1_0(parameters, src_desc->parameters, src_desc->parameter_count)) < 0)
            goto fail;
    }
    dst_desc->parameters = parameters;
    if ((dst_desc->static_sampler_count = src_desc->static_sampler_count))
    {
        if (!(samplers = vkd3d_calloc(dst_desc->static_sampler_count, sizeof(*samplers))))
        {
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
            goto fail;
        }

        if (src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_2)
        {
            /* Silently drop the flags field. This is not an error in the runtime. */
            for (i = 0; i < src_desc2->static_sampler_count; i++)
                convert_static_sampler_to_v_1_0(&samplers[i], &src_desc2->static_samplers[i]);
        }
        else
            memcpy(samplers, src_desc->static_samplers, src_desc->static_sampler_count * sizeof(*samplers));
    }
    dst_desc->static_samplers = samplers;
    dst_desc->flags = src_desc->flags;

    return VKD3D_OK;

fail:
    free_descriptor_ranges(parameters, dst_desc->parameter_count);
    vkd3d_free(parameters);
    vkd3d_free(samplers);
    return ret;
}

static void free_descriptor_ranges1(const struct vkd3d_root_parameter1 *parameters, unsigned int count)
{
    unsigned int i;

    if (!parameters)
        return;

    for (i = 0; i < count; ++i)
    {
        const struct vkd3d_root_parameter1 *p = &parameters[i];

        if (p->parameter_type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)p->descriptor_table.descriptor_ranges);
    }
}

static int dup_root_parameters_v_1_1(struct vkd3d_root_parameter1 *dst,
        const struct vkd3d_root_parameter1 *src, unsigned int count)
{
    struct vkd3d_descriptor_range1 *ranges;
    unsigned int i;
    int ret;

    for (i = 0; i < count; ++i)
    {
        const struct vkd3d_root_parameter1 *src_param = &src[i];
        struct vkd3d_root_parameter1 *dst_param = &dst[i];

        dst_param->parameter_type = src_param->parameter_type;
        switch (src_param->parameter_type)
        {
            case VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                ranges = NULL;
                if ((dst_param->descriptor_table.descriptor_range_count = src_param->descriptor_table.descriptor_range_count))
                {
                    if (!(ranges = vkd3d_calloc(src_param->descriptor_table.descriptor_range_count, sizeof(*ranges))))
                    {
                        ret = VKD3D_ERROR_OUT_OF_MEMORY;
                        goto fail;
                    }
                    memcpy(ranges, src_param->descriptor_table.descriptor_ranges, sizeof(*ranges) *
                            src_param->descriptor_table.descriptor_range_count);
                }
                dst_param->descriptor_table.descriptor_ranges = ranges;
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                dst_param->constants = src_param->constants;
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_CBV:
            case VKD3D_ROOT_PARAMETER_TYPE_SRV:
            case VKD3D_ROOT_PARAMETER_TYPE_UAV:
                dst_param->descriptor = src_param->descriptor;
                break;
            default:
                WARN("Invalid root parameter type %#x.\n", src_param->parameter_type);
                ret = VKD3D_ERROR_INVALID_ARGUMENT;
                goto fail;

        }
        dst_param->shader_visibility = src_param->shader_visibility;
    }

    return VKD3D_OK;

fail:
    free_descriptor_ranges1(dst, i);
    return ret;
}

static int convert_root_parameters_to_v_1_1(struct vkd3d_root_parameter1 *dst,
        const struct vkd3d_root_parameter *src, unsigned int count)
{
    const struct vkd3d_descriptor_range *ranges;
    struct vkd3d_descriptor_range1 *ranges1;
    unsigned int i, j;
    int ret;

    for (i = 0; i < count; ++i)
    {
        const struct vkd3d_root_parameter *p = &src[i];
        struct vkd3d_root_parameter1 *p1 = &dst[i];

        p1->parameter_type = p->parameter_type;
        switch (p1->parameter_type)
        {
            case VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                ranges1 = NULL;
                if ((p1->descriptor_table.descriptor_range_count = p->descriptor_table.descriptor_range_count))
                {
                    if (!(ranges1 = vkd3d_calloc(p1->descriptor_table.descriptor_range_count, sizeof(*ranges1))))
                    {
                        ret = VKD3D_ERROR_OUT_OF_MEMORY;
                        goto fail;
                    }
                }
                p1->descriptor_table.descriptor_ranges = ranges1;
                ranges = p->descriptor_table.descriptor_ranges;
                for (j = 0; j < p1->descriptor_table.descriptor_range_count; ++j)
                {
                    ranges1[j].range_type = ranges[j].range_type;
                    ranges1[j].descriptor_count = ranges[j].descriptor_count;
                    ranges1[j].base_shader_register = ranges[j].base_shader_register;
                    ranges1[j].register_space = ranges[j].register_space;
                    ranges1[j].flags = VKD3D_ROOT_SIGNATURE_1_0_DESCRIPTOR_RANGE_FLAGS;
                    ranges1[j].descriptor_table_offset = ranges[j].descriptor_table_offset;
                }
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                p1->constants = p->constants;
                break;
            case VKD3D_ROOT_PARAMETER_TYPE_CBV:
            case VKD3D_ROOT_PARAMETER_TYPE_SRV:
            case VKD3D_ROOT_PARAMETER_TYPE_UAV:
                p1->descriptor.shader_register = p->descriptor.shader_register;
                p1->descriptor.register_space = p->descriptor.register_space;
                p1->descriptor.flags = VKD3D_ROOT_SIGNATURE_1_0_ROOT_DESCRIPTOR_FLAGS;
                break;
            default:
                WARN("Invalid root parameter type %#x.\n", p1->parameter_type);
                ret = VKD3D_ERROR_INVALID_ARGUMENT;
                goto fail;

        }
        p1->shader_visibility = p->shader_visibility;
    }

    return VKD3D_OK;

fail:
    free_descriptor_ranges1(dst, i);
    return ret;
}

static int convert_root_signature_to_v1_1(struct vkd3d_versioned_root_signature_desc *dst,
        const struct vkd3d_versioned_root_signature_desc *src)
{
    const struct vkd3d_root_signature_desc2 *src_desc2 = &src->v_1_2;
    const struct vkd3d_root_signature_desc *src_desc = &src->v_1_0;
    struct vkd3d_root_signature_desc1 *dst_desc = &dst->v_1_1;
    struct vkd3d_static_sampler_desc *samplers = NULL;
    struct vkd3d_root_parameter1 *parameters = NULL;
    unsigned int i;
    int ret;

    assert(src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0 || src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_2);

    if ((dst_desc->parameter_count = src_desc->parameter_count))
    {
        if (!(parameters = vkd3d_calloc(dst_desc->parameter_count, sizeof(*parameters))))
        {
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
            goto fail;
        }

        if (src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_2)
        {
            if ((ret = dup_root_parameters_v_1_1(parameters,
                    src_desc2->parameters,
                    src_desc2->parameter_count)) < 0)
                goto fail;
        }
        else
        {
            if ((ret = convert_root_parameters_to_v_1_1(parameters,
                    src_desc->parameters,
                    src_desc->parameter_count)) < 0)
                goto fail;
        }
    }
    dst_desc->parameters = parameters;
    if ((dst_desc->static_sampler_count = src_desc->static_sampler_count))
    {
        if (!(samplers = vkd3d_calloc(dst_desc->static_sampler_count, sizeof(*samplers))))
        {
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
            goto fail;
        }

        if (src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_2)
        {
            /* Silently drop the flags field. This is not an error in the runtime. */
            for (i = 0; i < src_desc2->static_sampler_count; i++)
                convert_static_sampler_to_v_1_0(&samplers[i], &src_desc2->static_samplers[i]);
        }
        else
            memcpy(samplers, src_desc->static_samplers, src_desc->static_sampler_count * sizeof(*samplers));
    }
    dst_desc->static_samplers = samplers;
    dst_desc->flags = src_desc->flags;

    return VKD3D_OK;

fail:
    free_descriptor_ranges1(parameters, dst_desc->parameter_count);
    vkd3d_free(parameters);
    vkd3d_free(samplers);
    return ret;
}

static int convert_root_signature_to_v1_2(struct vkd3d_versioned_root_signature_desc *dst,
        const struct vkd3d_versioned_root_signature_desc *src)
{
    const struct vkd3d_root_signature_desc1 *src_desc1 = &src->v_1_1;
    const struct vkd3d_root_signature_desc *src_desc = &src->v_1_0;
    struct vkd3d_root_signature_desc2 *dst_desc = &dst->v_1_2;
    struct vkd3d_static_sampler_desc1 *samplers = NULL;
    struct vkd3d_root_parameter1 *parameters = NULL;
    unsigned int i;
    int ret;

    assert(src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0 || src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_1);

    if ((dst_desc->parameter_count = src_desc->parameter_count))
    {
        if (!(parameters = vkd3d_calloc(dst_desc->parameter_count, sizeof(*parameters))))
        {
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
            goto fail;
        }

        if (src->version == VKD3D_ROOT_SIGNATURE_VERSION_1_1)
        {
            if ((ret = dup_root_parameters_v_1_1(parameters,
                    src_desc1->parameters,
                    src_desc1->parameter_count)) < 0)
                goto fail;
        }
        else
        {
            if ((ret = convert_root_parameters_to_v_1_1(parameters,
                    src_desc->parameters,
                    src_desc->parameter_count)) < 0)
                goto fail;
        }
    }
    dst_desc->parameters = parameters;
    if ((dst_desc->static_sampler_count = src_desc->static_sampler_count))
    {
        if (!(samplers = vkd3d_calloc(dst_desc->static_sampler_count, sizeof(*samplers))))
        {
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
            goto fail;
        }

        for (i = 0; i < src_desc1->static_sampler_count; i++)
            convert_static_sampler_to_v_1_2(&samplers[i], &src_desc1->static_samplers[i]);
    }
    dst_desc->static_samplers = samplers;
    dst_desc->flags = src_desc->flags;

    return VKD3D_OK;

fail:
    free_descriptor_ranges1(parameters, dst_desc->parameter_count);
    vkd3d_free(parameters);
    vkd3d_free(samplers);
    return ret;
}

int vkd3d_shader_convert_root_signature(struct vkd3d_versioned_root_signature_desc *dst,
        enum vkd3d_root_signature_version version, const struct vkd3d_versioned_root_signature_desc *src)
{
    int ret;

    TRACE("dst %p, version %#x, src %p.\n", dst, version, src);

    if (src->version == version)
    {
        WARN("Nothing to convert.\n");
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if (!vkd3d_root_signature_version_is_supported(version))
    {
        WARN("Root signature version %#x not supported.\n", version);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if (!vkd3d_root_signature_version_is_supported(src->version))
    {
        WARN("Root signature version %#x not supported.\n", src->version);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    memset(dst, 0, sizeof(*dst));
    dst->version = version;

    if (version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        ret = convert_root_signature_to_v1_0(dst, src);
    }
    else if (version == VKD3D_ROOT_SIGNATURE_VERSION_1_1)
    {
        ret = convert_root_signature_to_v1_1(dst, src);
    }
    else
    {
        assert(version == VKD3D_ROOT_SIGNATURE_VERSION_1_2);
        ret = convert_root_signature_to_v1_2(dst, src);
    }

    return ret;
}
