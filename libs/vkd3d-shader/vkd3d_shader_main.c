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

struct vkd3d_shader_parser
{
    struct vkd3d_shader_desc shader_desc;
    struct vkd3d_shader_version shader_version;
    void *data;
    const DWORD *ptr;
};

static int vkd3d_shader_parser_init(struct vkd3d_shader_parser *parser,
        const struct vkd3d_shader_code *dxbc)
{
    struct vkd3d_shader_desc *shader_desc = &parser->shader_desc;
    int ret;

    if ((ret = shader_extract_from_dxbc(dxbc->code, dxbc->size, shader_desc)) < 0)
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

int vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv, uint32_t compiler_options,
        const struct vkd3d_shader_interface *shader_interface)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_dxbc_compiler *spirv_compiler;
    struct vkd3d_shader_scan_info scan_info;
    struct vkd3d_shader_parser parser;
    int ret;

    TRACE("dxbc {%p, %zu}, spirv %p, compiler_options %#x, shader_interface %p.\n",
            dxbc->code, dxbc->size, spirv, compiler_options, shader_interface);

    if ((ret = vkd3d_shader_scan_dxbc(dxbc, &scan_info)) < 0)
        return ret;

    if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
        return ret;

    if (!(spirv_compiler = vkd3d_dxbc_compiler_create(&parser.shader_version,
            &parser.shader_desc, compiler_options, shader_interface, &scan_info)))
    {
        ERR("Failed to create DXBC compiler.\n");
        vkd3d_shader_parser_destroy(&parser);
        return VKD3D_ERROR;
    }

    while (!shader_sm4_is_end(parser.data, &parser.ptr))
    {
        shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

        if (instruction.handler_idx == VKD3DSIH_TABLE_SIZE)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            vkd3d_dxbc_compiler_destroy(spirv_compiler);
            vkd3d_shader_parser_destroy(&parser);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        vkd3d_dxbc_compiler_handle_instruction(spirv_compiler, &instruction);
    }

    ret = vkd3d_dxbc_compiler_generate_spirv(spirv_compiler, spirv);
    vkd3d_dxbc_compiler_destroy(spirv_compiler);
    vkd3d_shader_parser_destroy(&parser);
    return ret;
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

static void vkd3d_shader_scan_record_uav_read(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    assert(reg->idx[0].offset < VKD3D_SHADER_MAX_UNORDERED_ACCESS_VIEWS);
    scan_info->uav_read_mask |= 1u << reg->idx[0].offset;
}

static bool vkd3d_shader_instruction_is_uav_counter(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return handler_idx == VKD3DSIH_IMM_ATOMIC_ALLOC
            || handler_idx == VKD3DSIH_IMM_ATOMIC_CONSUME;
}

static void vkd3d_shader_scan_record_uav_counter(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    assert(reg->idx[0].offset < VKD3D_SHADER_MAX_UNORDERED_ACCESS_VIEWS);
    scan_info->uav_counter_mask |= 1u << reg->idx[0].offset;
}

static void vkd3d_shader_scan_handle_instruction(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_instruction *instruction)
{
    unsigned int i;

    if (vkd3d_shader_instruction_is_uav_read(instruction))
    {
        for (i = 0; i < instruction->dst_count; ++i)
        {
            if (instruction->dst[i].reg.type == VKD3DSPR_UAV)
                vkd3d_shader_scan_record_uav_read(scan_info, &instruction->dst[i].reg);
        }
        for (i = 0; i < instruction->src_count; ++i)
        {
            if (instruction->src[i].reg.type == VKD3DSPR_UAV)
                vkd3d_shader_scan_record_uav_read(scan_info, &instruction->src[i].reg);
        }
    }

    if (vkd3d_shader_instruction_is_uav_counter(instruction))
        vkd3d_shader_scan_record_uav_counter(scan_info, &instruction->src[0].reg);
}

int vkd3d_shader_scan_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_shader_parser parser;
    int ret;

    TRACE("dxbc {%p, %zu}, scan_info %p.\n", dxbc->code, dxbc->size, scan_info);

    if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
        return ret;

    memset(scan_info, 0, sizeof(*scan_info));

    while (!shader_sm4_is_end(parser.data, &parser.ptr))
    {
        shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

        if (instruction.handler_idx == VKD3DSIH_TABLE_SIZE)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            vkd3d_shader_parser_destroy(&parser);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        vkd3d_shader_scan_handle_instruction(scan_info, &instruction);
    }

    vkd3d_shader_parser_destroy(&parser);
    return VKD3D_OK;
}

void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *shader_code)
{
    if (!shader_code)
        return;

    vkd3d_free((void *)shader_code->code);
}

void vkd3d_shader_free_root_signature(struct vkd3d_root_signature_desc *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->parameter_count; ++i)
    {
        const struct vkd3d_root_parameter *parameter = &root_signature->parameters[i];

        if (parameter->parameter_type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->u.descriptor_table.descriptor_ranges);
    }
    vkd3d_free((void *)root_signature->parameters);
    vkd3d_free((void *)root_signature->static_samplers);

    memset(root_signature, 0, sizeof(*root_signature));
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
    struct vkd3d_shader_signature_element *e = signature->elements;
    unsigned int i;

    TRACE("signature %p, semantic_name %s, semantic_index %u, stream_index %u.\n",
            signature, debugstr_a(semantic_name), semantic_index, stream_index);

    e = signature->elements;
    for (i = 0; i < signature->element_count; ++i)
    {
        if (!strcasecmp(e[i].semantic_name, semantic_name)
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
