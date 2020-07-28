/*
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

#include "vkd3d_shader_private.h"

#include <stdio.h>

VKD3D_DEBUG_ENV_NAME("VKD3D_SHADER_DEBUG");

static void vkd3d_string_buffer_clear(struct vkd3d_string_buffer *buffer)
{
    buffer->buffer[0] = '\0';
    buffer->content_size = 0;
}

bool vkd3d_string_buffer_init(struct vkd3d_string_buffer *buffer)
{
    buffer->buffer_size = 32;
    if (!(buffer->buffer = vkd3d_malloc(buffer->buffer_size)))
    {
        ERR("Failed to allocate shader buffer memory.\n");
        return false;
    }

    vkd3d_string_buffer_clear(buffer);
    return true;
}

void vkd3d_string_buffer_cleanup(struct vkd3d_string_buffer *buffer)
{
    vkd3d_free(buffer->buffer);
}

static bool vkd3d_string_buffer_resize(struct vkd3d_string_buffer *buffer, int rc)
{
    unsigned int new_buffer_size = buffer->buffer_size * 2;
    char *new_buffer;

    while (rc > 0 && (unsigned int)rc >= new_buffer_size - buffer->content_size)
        new_buffer_size *= 2;
    if (!(new_buffer = vkd3d_realloc(buffer->buffer, new_buffer_size)))
    {
        ERR("Failed to grow buffer.\n");
        buffer->buffer[buffer->content_size] = '\0';
        return false;
    }
    buffer->buffer = new_buffer;
    buffer->buffer_size = new_buffer_size;
    return true;
}

int vkd3d_string_buffer_vprintf(struct vkd3d_string_buffer *buffer, const char *format, va_list args)
{
    unsigned int rem;
    va_list a;
    int rc;

    for (;;)
    {
        rem = buffer->buffer_size - buffer->content_size;
        va_copy(a, args);
        rc = vsnprintf(&buffer->buffer[buffer->content_size], rem, format, a);
        va_end(a);
        if (rc >= 0 && (unsigned int)rc < rem)
        {
            buffer->content_size += rc;
            return 0;
        }

        if (!vkd3d_string_buffer_resize(buffer, rc))
            return -1;
    }
}

static int VKD3D_PRINTF_FUNC(2, 3) vkd3d_string_buffer_printf(struct vkd3d_string_buffer *buffer,
        const char *format, ...)
{
    va_list args;
    int ret;

    va_start(args, format);
    ret = vkd3d_string_buffer_vprintf(buffer, format, args);
    va_end(args);

    return ret;
}

bool vkd3d_shader_message_context_init(struct vkd3d_shader_message_context *context,
        enum vkd3d_shader_log_level log_level, const char *source_name)
{
    context->log_level = log_level;
    context->source_name = source_name ? source_name : "<anonymous>";
    context->line = 0;
    context->column = 0;

    return vkd3d_string_buffer_init(&context->messages);
}

void vkd3d_shader_message_context_cleanup(struct vkd3d_shader_message_context *context)
{
    vkd3d_string_buffer_cleanup(&context->messages);
}

static char *vkd3d_shader_message_context_copy_messages(struct vkd3d_shader_message_context *context)
{
    char *messages;

    if ((messages = vkd3d_malloc(context->messages.content_size + 1)))
        memcpy(messages, context->messages.buffer, context->messages.content_size + 1);

    return messages;
}

void vkd3d_shader_error(struct vkd3d_shader_message_context *context,
        enum vkd3d_shader_error error, const char *format, ...)
{
    va_list args;

    if (context->log_level < VKD3D_SHADER_LOG_ERROR)
        return;

    if (context->line)
        vkd3d_string_buffer_printf(&context->messages, "%s:%u:%u: E%04u: ",
                context->source_name, context->line, context->column, error);
    else
        vkd3d_string_buffer_printf(&context->messages, "%s: E%04u: ", context->source_name, error);
    va_start(args, format);
    vkd3d_string_buffer_vprintf(&context->messages, format, args);
    va_end(args);
    vkd3d_string_buffer_printf(&context->messages, "\n");
}

static void vkd3d_shader_dump_blob(const char *path, const char *prefix, const void *data, size_t size)
{
    static int shader_id = 0;
    char filename[1024];
    unsigned int id;
    FILE *f;

    id = InterlockedIncrement(&shader_id) - 1;

    snprintf(filename, ARRAY_SIZE(filename), "%s/vkd3d-shader-%s-%u.dxbc", path, prefix, id);
    if ((f = fopen(filename, "wb")))
    {
        if (fwrite(data, 1, size, f) != size)
            ERR("Failed to write shader to %s.\n", filename);
        if (fclose(f))
            ERR("Failed to close stream %s.\n", filename);
    }
    else
    {
        ERR("Failed to open %s for dumping shader.\n", filename);
    }
}

static void vkd3d_shader_dump_shader(enum vkd3d_shader_type type, const struct vkd3d_shader_code *shader)
{
    static bool enabled = true;
    const char *path;

    if (!enabled)
        return;

    if (!(path = getenv("VKD3D_SHADER_DUMP_PATH")))
    {
        enabled = false;
        return;
    }

    vkd3d_shader_dump_blob(path, shader_get_type_prefix(type), shader->code, shader->size);
}

struct vkd3d_shader_parser
{
    struct vkd3d_shader_desc shader_desc;
    struct vkd3d_shader_version shader_version;
    void *data;
    const DWORD *ptr;
};

static int vkd3d_shader_parser_init(struct vkd3d_shader_parser *parser,
        const struct vkd3d_shader_code *dxbc, struct vkd3d_shader_message_context *message_context)
{
    struct vkd3d_shader_desc *shader_desc = &parser->shader_desc;
    int ret;

    if ((ret = shader_extract_from_dxbc(dxbc->code, dxbc->size, message_context, shader_desc)) < 0)
    {
        WARN("Failed to extract shader, vkd3d result %d.\n", ret);
        return ret;
    }

    if (!(parser->data = shader_sm4_init(shader_desc->byte_code,
            shader_desc->byte_code_size, &shader_desc->output_signature)))
    {
        WARN("Failed to initialize shader parser.\n");
        free_shader_desc(shader_desc);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    shader_sm4_read_header(parser->data, &parser->ptr, &parser->shader_version);
    return VKD3D_OK;
}

static void vkd3d_shader_parser_destroy(struct vkd3d_shader_parser *parser)
{
    shader_sm4_free(parser->data);
    free_shader_desc(&parser->shader_desc);
}

static int vkd3d_shader_validate_compile_info(const struct vkd3d_shader_compile_info *compile_info)
{
    if (compile_info->type != VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO)
    {
        WARN("Invalid structure type %#x.\n", compile_info->type);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    switch (compile_info->source_type)
    {
        case VKD3D_SHADER_SOURCE_DXBC_TPF:
            break;
        default:
            WARN("Invalid shader source type %#x.\n", compile_info->source_type);
            return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    switch (compile_info->target_type)
    {
        case VKD3D_SHADER_TARGET_SPIRV_BINARY:
            break;
        default:
            WARN("Invalid shader target type %#x.\n", compile_info->target_type);
            return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    return VKD3D_OK;
}

void vkd3d_shader_free_messages(char *messages)
{
    vkd3d_free(messages);
}

int vkd3d_shader_compile(const struct vkd3d_shader_compile_info *compile_info,
        struct vkd3d_shader_code *out, char **messages)
{
    struct vkd3d_shader_message_context message_context;
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_dxbc_compiler *spirv_compiler;
    struct vkd3d_shader_scan_info scan_info;
    struct vkd3d_shader_parser parser;
    int ret;

    TRACE("compile_info %p, out %p, messages %p.\n", compile_info, out, messages);

    if (messages)
        *messages = NULL;

    if ((ret = vkd3d_shader_validate_compile_info(compile_info)) < 0)
        return ret;

    scan_info.type = VKD3D_SHADER_STRUCTURE_TYPE_SCAN_INFO;
    scan_info.next = NULL;
    if ((ret = vkd3d_shader_scan_dxbc(&compile_info->source, &scan_info, messages)) < 0)
        return ret;

    if (!vkd3d_shader_message_context_init(&message_context, compile_info->log_level, compile_info->source_name))
        return VKD3D_ERROR;
    ret = vkd3d_shader_parser_init(&parser, &compile_info->source, &message_context);
    if (messages)
    {
        vkd3d_shader_free_messages(*messages);
        if (!(*messages = vkd3d_shader_message_context_copy_messages(&message_context)))
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
    }
    vkd3d_shader_message_context_cleanup(&message_context);
    if (ret < 0)
        return ret;

    vkd3d_shader_dump_shader(parser.shader_version.type, &compile_info->source);

    if (!(spirv_compiler = vkd3d_dxbc_compiler_create(&parser.shader_version,
            &parser.shader_desc, compile_info, &scan_info)))
    {
        ERR("Failed to create DXBC compiler.\n");
        vkd3d_shader_parser_destroy(&parser);
        return VKD3D_ERROR;
    }

    while (!shader_sm4_is_end(parser.data, &parser.ptr))
    {
        shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

        if (instruction.handler_idx == VKD3DSIH_INVALID)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            vkd3d_dxbc_compiler_destroy(spirv_compiler);
            vkd3d_shader_parser_destroy(&parser);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        if ((ret = vkd3d_dxbc_compiler_handle_instruction(spirv_compiler, &instruction)) < 0)
            break;
    }

    if (ret >= 0)
        ret = vkd3d_dxbc_compiler_generate_spirv(spirv_compiler, out);

    vkd3d_dxbc_compiler_destroy(spirv_compiler);
    vkd3d_shader_parser_destroy(&parser);
    vkd3d_shader_free_scan_info(&scan_info);
    return ret;
}

struct vkd3d_shader_scan_context
{
    struct vkd3d_shader_scan_info *scan_info;
    size_t descriptors_size;

    struct
    {
        unsigned int id;
        unsigned int descriptor_idx;
    } *uav_ranges;
    size_t uav_ranges_size;
    size_t uav_range_count;
};

static struct vkd3d_shader_descriptor_info *vkd3d_shader_scan_get_uav_descriptor_info(
        const struct vkd3d_shader_scan_context *context, unsigned int range_id)
{
    unsigned int i;

    for (i = 0; i < context->uav_range_count; ++i)
    {
        if (context->uav_ranges[i].id == range_id)
            return &context->scan_info->descriptors[context->uav_ranges[i].descriptor_idx];
    }

    return NULL;
}

static bool vkd3d_shader_instruction_is_uav_read(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return (VKD3DSIH_ATOMIC_AND <= handler_idx && handler_idx <= VKD3DSIH_ATOMIC_XOR)
            || (VKD3DSIH_IMM_ATOMIC_ALLOC <= handler_idx && handler_idx <= VKD3DSIH_IMM_ATOMIC_XOR)
            || handler_idx == VKD3DSIH_LD_UAV_TYPED
            || (handler_idx == VKD3DSIH_LD_RAW && instruction->src[1].reg.type == VKD3DSPR_UAV)
            || (handler_idx == VKD3DSIH_LD_STRUCTURED && instruction->src[2].reg.type == VKD3DSPR_UAV);
}

static void vkd3d_shader_scan_record_uav_read(struct vkd3d_shader_scan_context *context,
        const struct vkd3d_shader_register *reg)
{
    struct vkd3d_shader_descriptor_info *d;

    d = vkd3d_shader_scan_get_uav_descriptor_info(context, reg->idx[0].offset);
    d->flags |= VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_UAV_READ;
}

static bool vkd3d_shader_instruction_is_uav_counter(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return handler_idx == VKD3DSIH_IMM_ATOMIC_ALLOC
            || handler_idx == VKD3DSIH_IMM_ATOMIC_CONSUME;
}

static void vkd3d_shader_scan_record_uav_counter(struct vkd3d_shader_scan_context *context,
        const struct vkd3d_shader_register *reg)
{
    struct vkd3d_shader_descriptor_info *d;

    d = vkd3d_shader_scan_get_uav_descriptor_info(context, reg->idx[0].offset);
    d->flags |= VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_UAV_COUNTER;
}

static bool vkd3d_shader_scan_add_descriptor(struct vkd3d_shader_scan_context *context,
        enum vkd3d_shader_descriptor_type type, unsigned int register_space, unsigned int register_index,
        enum vkd3d_shader_resource_type resource_type, enum vkd3d_shader_resource_data_type resource_data_type,
        unsigned int flags)
{
    struct vkd3d_shader_scan_info *scan_info = context->scan_info;
    struct vkd3d_shader_descriptor_info *d;

    if (!vkd3d_array_reserve((void **)&scan_info->descriptors, &context->descriptors_size,
            scan_info->descriptor_count + 1, sizeof(*scan_info->descriptors)))
    {
        ERR("Failed to allocate descriptor info.\n");
        return false;
    }

    d = &scan_info->descriptors[scan_info->descriptor_count];
    d->type = type;
    d->register_space = register_space;
    d->register_index = register_index;
    d->resource_type = resource_type;
    d->resource_data_type = resource_data_type;
    d->flags = flags;
    d->count = 1;
    ++scan_info->descriptor_count;

    return true;
}

static bool vkd3d_shader_scan_add_uav_range(struct vkd3d_shader_scan_context *context,
        unsigned int id, unsigned int descriptor_idx)
{
    if (!vkd3d_array_reserve((void **)&context->uav_ranges, &context->uav_ranges_size,
            context->uav_range_count + 1, sizeof(*context->uav_ranges)))
    {
        ERR("Failed to allocate UAV range.\n");
        return false;
    }

    context->uav_ranges[context->uav_range_count].id = id;
    context->uav_ranges[context->uav_range_count].descriptor_idx = descriptor_idx;
    ++context->uav_range_count;

    return true;
}

static void vkd3d_shader_scan_constant_buffer_declaration(struct vkd3d_shader_scan_context *context,
        const struct vkd3d_shader_instruction *instruction)
{
    const struct vkd3d_shader_constant_buffer *cb = &instruction->declaration.cb;

    vkd3d_shader_scan_add_descriptor(context, VKD3D_SHADER_DESCRIPTOR_TYPE_CBV, cb->register_space,
            cb->register_index, VKD3D_SHADER_RESOURCE_BUFFER, VKD3D_SHADER_RESOURCE_DATA_UINT, 0);
}

static void vkd3d_shader_scan_sampler_declaration(struct vkd3d_shader_scan_context *context,
        const struct vkd3d_shader_instruction *instruction)
{
    const struct vkd3d_shader_sampler *sampler = &instruction->declaration.sampler;
    unsigned int flags;

    if (instruction->flags & VKD3DSI_SAMPLER_COMPARISON_MODE)
        flags = VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_SAMPLER_COMPARISON_MODE;
    else
        flags = 0;
    vkd3d_shader_scan_add_descriptor(context, VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER, sampler->register_space,
            sampler->register_index, VKD3D_SHADER_RESOURCE_NONE, VKD3D_SHADER_RESOURCE_DATA_UINT, flags);
}

static void vkd3d_shader_scan_resource_declaration(struct vkd3d_shader_scan_context *context,
        const struct vkd3d_shader_resource *resource, enum vkd3d_shader_resource_type resource_type,
        enum vkd3d_shader_resource_data_type resource_data_type)
{
    enum vkd3d_shader_descriptor_type type;

    if (resource->reg.reg.type == VKD3DSPR_UAV)
        type = VKD3D_SHADER_DESCRIPTOR_TYPE_UAV;
    else
        type = VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
    vkd3d_shader_scan_add_descriptor(context, type, resource->register_space,
            resource->register_index, resource_type, resource_data_type, 0);
    if (type == VKD3D_SHADER_DESCRIPTOR_TYPE_UAV)
        vkd3d_shader_scan_add_uav_range(context, resource->reg.reg.idx[0].offset,
                context->scan_info->descriptor_count - 1);
}

static void vkd3d_shader_scan_typed_resource_declaration(struct vkd3d_shader_scan_context *context,
        const struct vkd3d_shader_instruction *instruction)
{
    const struct vkd3d_shader_semantic *semantic = &instruction->declaration.semantic;
    enum vkd3d_shader_resource_data_type resource_data_type;

    switch (semantic->resource_data_type)
    {
        case VKD3D_DATA_UNORM:
            resource_data_type = VKD3D_SHADER_RESOURCE_DATA_UNORM;
            break;
        case VKD3D_DATA_SNORM:
            resource_data_type = VKD3D_SHADER_RESOURCE_DATA_SNORM;
            break;
        case VKD3D_DATA_INT:
            resource_data_type = VKD3D_SHADER_RESOURCE_DATA_INT;
            break;
        case VKD3D_DATA_UINT:
            resource_data_type = VKD3D_SHADER_RESOURCE_DATA_UINT;
            break;
        case VKD3D_DATA_FLOAT:
            resource_data_type = VKD3D_SHADER_RESOURCE_DATA_FLOAT;
            break;
        default:
            ERR("Invalid resource data type %#x.\n", semantic->resource_data_type);
            resource_data_type = VKD3D_SHADER_RESOURCE_DATA_FLOAT;
            break;
    }
    vkd3d_shader_scan_resource_declaration(context, &semantic->resource,
            semantic->resource_type, resource_data_type);
}

static void vkd3d_shader_scan_instruction(struct vkd3d_shader_scan_context *context,
        const struct vkd3d_shader_instruction *instruction)
{
    unsigned int i;

    switch (instruction->handler_idx)
    {
        case VKD3DSIH_DCL_CONSTANT_BUFFER:
            vkd3d_shader_scan_constant_buffer_declaration(context, instruction);
            break;
        case VKD3DSIH_DCL_SAMPLER:
            vkd3d_shader_scan_sampler_declaration(context, instruction);
            break;
        case VKD3DSIH_DCL:
        case VKD3DSIH_DCL_UAV_TYPED:
            vkd3d_shader_scan_typed_resource_declaration(context, instruction);
            break;
        case VKD3DSIH_DCL_RESOURCE_RAW:
        case VKD3DSIH_DCL_UAV_RAW:
            vkd3d_shader_scan_resource_declaration(context, &instruction->declaration.raw_resource.resource,
                    VKD3D_SHADER_RESOURCE_BUFFER, VKD3D_SHADER_RESOURCE_DATA_UINT);
            break;
        case VKD3DSIH_DCL_RESOURCE_STRUCTURED:
        case VKD3DSIH_DCL_UAV_STRUCTURED:
            vkd3d_shader_scan_resource_declaration(context, &instruction->declaration.structured_resource.resource,
                    VKD3D_SHADER_RESOURCE_BUFFER, VKD3D_SHADER_RESOURCE_DATA_UINT);
            break;
        default:
            break;
    }

    if (vkd3d_shader_instruction_is_uav_read(instruction))
    {
        for (i = 0; i < instruction->dst_count; ++i)
        {
            if (instruction->dst[i].reg.type == VKD3DSPR_UAV)
                vkd3d_shader_scan_record_uav_read(context, &instruction->dst[i].reg);
        }
        for (i = 0; i < instruction->src_count; ++i)
        {
            if (instruction->src[i].reg.type == VKD3DSPR_UAV)
                vkd3d_shader_scan_record_uav_read(context, &instruction->src[i].reg);
        }
    }

    if (vkd3d_shader_instruction_is_uav_counter(instruction))
        vkd3d_shader_scan_record_uav_counter(context, &instruction->src[0].reg);
}

int vkd3d_shader_scan_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info, char **messages)
{
    struct vkd3d_shader_message_context message_context;
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_shader_scan_context context;
    struct vkd3d_shader_parser parser;
    int ret;

    TRACE("dxbc {%p, %zu}, scan_info %p, messages %p.\n", dxbc->code, dxbc->size, scan_info, messages);

    if (messages)
        *messages = NULL;

    if (scan_info->type != VKD3D_SHADER_STRUCTURE_TYPE_SCAN_INFO)
    {
        WARN("Invalid structure type %#x.\n", scan_info->type);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if (!vkd3d_shader_message_context_init(&message_context, VKD3D_SHADER_LOG_INFO, NULL))
        return VKD3D_ERROR;
    ret = vkd3d_shader_parser_init(&parser, dxbc, &message_context);
    if (messages && !(*messages = vkd3d_shader_message_context_copy_messages(&message_context)))
        ret = VKD3D_ERROR_OUT_OF_MEMORY;
    vkd3d_shader_message_context_cleanup(&message_context);
    if (ret < 0)
        return ret;

    if (TRACE_ON())
        vkd3d_shader_trace(parser.data);

    memset(scan_info, 0, sizeof(*scan_info));

    memset(&context, 0, sizeof(context));
    context.scan_info = scan_info;

    while (!shader_sm4_is_end(parser.data, &parser.ptr))
    {
        shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

        if (instruction.handler_idx == VKD3DSIH_INVALID)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            vkd3d_free(context.uav_ranges);
            vkd3d_shader_free_scan_info(scan_info);
            vkd3d_shader_parser_destroy(&parser);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        vkd3d_shader_scan_instruction(&context, &instruction);
    }

    vkd3d_free(context.uav_ranges);
    vkd3d_shader_parser_destroy(&parser);
    return VKD3D_OK;
}

void vkd3d_shader_free_scan_info(struct vkd3d_shader_scan_info *scan_info)
{
    if (!scan_info)
        return;

    vkd3d_free(scan_info->descriptors);
}

void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *shader_code)
{
    if (!shader_code)
        return;

    vkd3d_free((void *)shader_code->code);
}

static void vkd3d_shader_free_root_signature_v_1_0(struct vkd3d_shader_root_signature_desc *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->parameter_count; ++i)
    {
        const struct vkd3d_shader_root_parameter *parameter = &root_signature->parameters[i];

        if (parameter->parameter_type == VKD3D_SHADER_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->u.descriptor_table.descriptor_ranges);
    }
    vkd3d_free((void *)root_signature->parameters);
    vkd3d_free((void *)root_signature->static_samplers);

    memset(root_signature, 0, sizeof(*root_signature));
}

static void vkd3d_shader_free_root_signature_v_1_1(struct vkd3d_shader_root_signature_desc1 *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->parameter_count; ++i)
    {
        const struct vkd3d_shader_root_parameter1 *parameter = &root_signature->parameters[i];

        if (parameter->parameter_type == VKD3D_SHADER_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->u.descriptor_table.descriptor_ranges);
    }
    vkd3d_free((void *)root_signature->parameters);
    vkd3d_free((void *)root_signature->static_samplers);

    memset(root_signature, 0, sizeof(*root_signature));
}

void vkd3d_shader_free_root_signature(struct vkd3d_shader_versioned_root_signature_desc *desc)
{
    if (desc->version == VKD3D_SHADER_ROOT_SIGNATURE_VERSION_1_0)
    {
        vkd3d_shader_free_root_signature_v_1_0(&desc->u.v_1_0);
    }
    else if (desc->version == VKD3D_SHADER_ROOT_SIGNATURE_VERSION_1_1)
    {
        vkd3d_shader_free_root_signature_v_1_1(&desc->u.v_1_1);
    }
    else if (desc->version)
    {
        FIXME("Unknown version %#x.\n", desc->version);
        return;
    }

    desc->version = 0;
}

int vkd3d_shader_parse_input_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature)
{
    TRACE("dxbc {%p, %zu}, signature %p.\n", dxbc->code, dxbc->size, signature);

    return shader_parse_input_signature(dxbc->code, dxbc->size, signature);
}

struct vkd3d_shader_signature_element *vkd3d_shader_find_signature_element(
        const struct vkd3d_shader_signature *signature, const char *semantic_name,
        unsigned int semantic_index, unsigned int stream_index)
{
    struct vkd3d_shader_signature_element *e;
    unsigned int i;

    TRACE("signature %p, semantic_name %s, semantic_index %u, stream_index %u.\n",
            signature, debugstr_a(semantic_name), semantic_index, stream_index);

    e = signature->elements;
    for (i = 0; i < signature->element_count; ++i)
    {
        if (!ascii_strcasecmp(e[i].semantic_name, semantic_name)
                && e[i].semantic_index == semantic_index
                && e[i].stream_index == stream_index)
            return &e[i];
    }

    return NULL;
}

void vkd3d_shader_free_shader_signature(struct vkd3d_shader_signature *signature)
{
    TRACE("signature %p.\n", signature);

    vkd3d_free(signature->elements);
    signature->elements = NULL;
}
