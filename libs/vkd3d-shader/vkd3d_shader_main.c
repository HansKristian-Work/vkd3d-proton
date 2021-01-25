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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_SHADER

#include "vkd3d_shader_private.h"

#include <stdio.h>
#include <inttypes.h>

static void vkd3d_shader_dump_blob(const char *path, vkd3d_shader_hash_t hash, const void *data, size_t size, const char *ext)
{
    char filename[1024];
    FILE *f;

    snprintf(filename, ARRAY_SIZE(filename), "%s/%016"PRIx64".%s", path, hash, ext);

    /* Exclusive open to avoid multiple threads spamming out the same shader module, and avoids race condition. */
    if ((f = fopen(filename, "wbx")))
    {
        if (fwrite(data, 1, size, f) != size)
            ERR("Failed to write shader to %s.\n", filename);
        if (fclose(f))
            ERR("Failed to close stream %s.\n", filename);
    }
}

bool vkd3d_shader_replace(vkd3d_shader_hash_t hash, const void **data, size_t *size)
{
    static bool enabled = true;
    char filename[1024];
    void *buffer = NULL;
    const char *path;
    FILE *f = NULL;
    size_t len;

    if (!enabled)
        return false;

    if (!(path = getenv("VKD3D_SHADER_OVERRIDE")))
    {
        enabled = false;
        return false;
    }

    snprintf(filename, ARRAY_SIZE(filename), "%s/%016"PRIx64".spv", path, hash);
    if ((f = fopen(filename, "rb")))
    {
        if (fseek(f, 0, SEEK_END) < 0)
            goto err;
        len = ftell(f);
        if (len < 16)
            goto err;
        rewind(f);
        buffer = vkd3d_malloc(len);
        if (!buffer)
            goto err;
        if (fread(buffer, 1, len, f) != len)
            goto err;
    }
    else
        goto err;

    *data = buffer;
    *size = len;
    WARN("Overriding shader hash %016"PRIx64" with alternative SPIR-V module!\n", hash);
    fclose(f);
    return true;

err:
    if (f)
        fclose(f);
    vkd3d_free(buffer);
    return false;
}

void vkd3d_shader_dump_shader(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader, const char *ext)
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

    vkd3d_shader_dump_blob(path, hash, shader->code, shader->size, ext);
}

void vkd3d_shader_dump_spirv_shader(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader)
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

    vkd3d_shader_dump_blob(path, hash, shader->code, shader->size, "spv");
}

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

static int vkd3d_shader_validate_compile_args(const struct vkd3d_shader_compile_arguments *compile_args)
{
    if (!compile_args)
        return VKD3D_OK;

    if (compile_args->type != VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_ARGUMENTS)
    {
        WARN("Invalid structure type %#x.\n", compile_args->type);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    switch (compile_args->target)
    {
        case VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0:
            break;
        default:
            WARN("Invalid shader target %#x.\n", compile_args->target);
            return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    return VKD3D_OK;
}

struct vkd3d_shader_scan_key
{
    enum vkd3d_shader_register_type register_type;
    unsigned int register_id;
};

struct vkd3d_shader_scan_entry
{
    struct hash_map_entry entry;
    struct vkd3d_shader_scan_key key;
    unsigned int flags;
};

static uint32_t vkd3d_shader_scan_entry_hash(const void *key)
{
    const struct vkd3d_shader_scan_key *k = key;
    return hash_combine(k->register_type, k->register_id);
}

static bool vkd3d_shader_scan_entry_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_shader_scan_entry *e = (const struct vkd3d_shader_scan_entry*) entry;
    const struct vkd3d_shader_scan_key *k = key;
    return e->key.register_type == k->register_type && e->key.register_id == k->register_id;
}

unsigned int vkd3d_shader_scan_get_register_flags(const struct vkd3d_shader_scan_info *scan_info,
        enum vkd3d_shader_register_type type, unsigned int id)
{
    struct vkd3d_shader_scan_key key;
    struct hash_map_entry *e;

    key.register_type = type;
    key.register_id = id;

    e = hash_map_find(&scan_info->register_map, &key);
    return e ? e->flags : 0u;
}

static void vkd3d_shader_scan_set_register_flags(struct vkd3d_shader_scan_info *scan_info,
        enum vkd3d_shader_register_type type, unsigned int id, unsigned int flags)
{
    struct vkd3d_shader_scan_entry entry;
    struct vkd3d_shader_scan_key key;
    struct hash_map_entry *e;

    key.register_type = type;
    key.register_id = id;

    if ((e = hash_map_find(&scan_info->register_map, &key)))
        e->flags |= flags;
    else
    {
        entry.key = key;
        entry.flags = flags;
        hash_map_insert(&scan_info->register_map, &key, &entry.entry);
    }
}

static void vkd3d_shader_scan_init(struct vkd3d_shader_scan_info *scan_info)
{
    memset(scan_info, 0, sizeof(*scan_info));
    hash_map_init(&scan_info->register_map, &vkd3d_shader_scan_entry_hash,
            &vkd3d_shader_scan_entry_compare, sizeof(struct vkd3d_shader_scan_entry));
}

static void vkd3d_shader_scan_destroy(struct vkd3d_shader_scan_info *scan_info)
{
    hash_map_clear(&scan_info->register_map);
}

int vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv, unsigned int compiler_options,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compile_args)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_dxbc_compiler *spirv_compiler;
    struct vkd3d_shader_scan_info scan_info;
    struct vkd3d_shader_parser parser;
    vkd3d_shader_hash_t hash;
    int ret;

    TRACE("dxbc {%p, %zu}, spirv %p, compiler_options %#x, shader_interface_info %p, compile_args %p.\n",
            dxbc->code, dxbc->size, spirv, compiler_options, shader_interface_info, compile_args);

    if (shader_interface_info && shader_interface_info->type != VKD3D_SHADER_STRUCTURE_TYPE_SHADER_INTERFACE_INFO)
    {
        WARN("Invalid structure type %#x.\n", shader_interface_info->type);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if ((ret = vkd3d_shader_validate_compile_args(compile_args)) < 0)
        return ret;

    /* DXIL is handled externally through dxil-spirv. */
    if (shader_is_dxil(dxbc->code, dxbc->size))
    {
        return vkd3d_shader_compile_dxil(dxbc, spirv, shader_interface_info, compile_args);
    }

    hash = vkd3d_shader_hash(dxbc);
    spirv->meta.replaced = false;
    spirv->meta.hash = hash;
    if (vkd3d_shader_replace(hash, &spirv->code, &spirv->size))
    {
        spirv->meta.replaced = true;
        return VKD3D_OK;
    }

    vkd3d_shader_scan_init(&scan_info);

    if ((ret = vkd3d_shader_scan_dxbc(dxbc, &scan_info)) < 0)
    {
        vkd3d_shader_scan_destroy(&scan_info);
        return ret;
    }

    if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
    {
        vkd3d_shader_scan_destroy(&scan_info);
        return ret;
    }

    vkd3d_shader_dump_shader(hash, dxbc, "dxbc");

    if (TRACE_ON())
        vkd3d_shader_trace(parser.data);

    if (!(spirv_compiler = vkd3d_dxbc_compiler_create(&parser.shader_version,
            &parser.shader_desc, compiler_options, shader_interface_info, compile_args, &scan_info)))
    {
        ERR("Failed to create DXBC compiler.\n");
        vkd3d_shader_scan_destroy(&scan_info);
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
            vkd3d_shader_scan_destroy(&scan_info);
            vkd3d_shader_parser_destroy(&parser);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        if ((ret = vkd3d_dxbc_compiler_handle_instruction(spirv_compiler, &instruction)) < 0)
            break;
    }

    if (ret >= 0)
        ret = vkd3d_dxbc_compiler_generate_spirv(spirv_compiler, spirv);

    if (ret == 0)
        vkd3d_shader_dump_spirv_shader(hash, spirv);

    vkd3d_dxbc_compiler_destroy(spirv_compiler);
    vkd3d_shader_scan_destroy(&scan_info);
    vkd3d_shader_parser_destroy(&parser);
    return ret;
}

static bool vkd3d_shader_instruction_is_uav_read(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return (VKD3DSIH_ATOMIC_AND <= handler_idx && handler_idx <= VKD3DSIH_ATOMIC_XOR)
            || (VKD3DSIH_IMM_ATOMIC_ALLOC <= handler_idx && handler_idx <= VKD3DSIH_IMM_ATOMIC_XOR)
            || handler_idx == VKD3DSIH_LD_UAV_TYPED || handler_idx == VKD3DSIH_LD_UAV_TYPED_FEEDBACK
            || ((handler_idx == VKD3DSIH_LD_RAW || handler_idx == VKD3DSIH_LD_RAW_FEEDBACK) && instruction->src[1].reg.type == VKD3DSPR_UAV)
            || ((handler_idx == VKD3DSIH_LD_STRUCTURED || handler_idx == VKD3DSIH_LD_STRUCTURED_FEEDBACK) && instruction->src[2].reg.type == VKD3DSPR_UAV);
}

static bool vkd3d_shader_instruction_is_uav_atomic(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return ((VKD3DSIH_ATOMIC_AND <= handler_idx && handler_idx <= VKD3DSIH_ATOMIC_XOR) ||
            (VKD3DSIH_IMM_ATOMIC_AND <= handler_idx && handler_idx <= VKD3DSIH_IMM_ATOMIC_XOR)) &&
            handler_idx != VKD3DSIH_IMM_ATOMIC_CONSUME;
}

static void vkd3d_shader_scan_record_uav_read(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    vkd3d_shader_scan_set_register_flags(scan_info, VKD3DSPR_UAV,
            reg->idx[0].offset, VKD3D_SHADER_UAV_FLAG_READ_ACCESS);
}

static void vkd3d_shader_scan_record_uav_atomic(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    vkd3d_shader_scan_set_register_flags(scan_info, VKD3DSPR_UAV,
            reg->idx[0].offset, VKD3D_SHADER_UAV_FLAG_ATOMIC_ACCESS);
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
    vkd3d_shader_scan_set_register_flags(scan_info, VKD3DSPR_UAV,
            reg->idx[0].offset, VKD3D_SHADER_UAV_FLAG_ATOMIC_COUNTER);
}

static void vkd3d_shader_scan_input_declaration(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_instruction *instruction)
{
    const struct vkd3d_shader_dst_param *dst = &instruction->declaration.dst;

    if (dst->reg.type == VKD3DSPR_OUTCONTROLPOINT)
        scan_info->use_vocp = true;
}

static void vkd3d_shader_scan_instruction(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_instruction *instruction)
{
    unsigned int i;
    bool is_atomic;

    switch (instruction->handler_idx)
    {
        case VKD3DSIH_DCL_INPUT:
            vkd3d_shader_scan_input_declaration(scan_info, instruction);
            break;
        default:
            break;
    }

    if (vkd3d_shader_instruction_is_uav_read(instruction))
    {
        is_atomic = vkd3d_shader_instruction_is_uav_atomic(instruction);

        for (i = 0; i < instruction->dst_count; ++i)
        {
            if (instruction->dst[i].reg.type == VKD3DSPR_UAV)
            {
                vkd3d_shader_scan_record_uav_read(scan_info, &instruction->dst[i].reg);
                if (is_atomic)
                    vkd3d_shader_scan_record_uav_atomic(scan_info, &instruction->dst[i].reg);
            }
        }
        for (i = 0; i < instruction->src_count; ++i)
        {
            if (instruction->src[i].reg.type == VKD3DSPR_UAV)
            {
                vkd3d_shader_scan_record_uav_read(scan_info, &instruction->src[i].reg);
                if (is_atomic)
                    vkd3d_shader_scan_record_uav_atomic(scan_info, &instruction->src[i].reg);
            }
        }
    }

    if (vkd3d_shader_instruction_is_uav_counter(instruction))
        vkd3d_shader_scan_record_uav_counter(scan_info, &instruction->src[0].reg);
}

int vkd3d_shader_scan_patch_vertex_count(const struct vkd3d_shader_code *dxbc,
        unsigned int *patch_vertex_count)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_shader_parser parser;
    int ret;

    if (shader_is_dxil(dxbc->code, dxbc->size))
    {
        /* TODO */
        *patch_vertex_count = 0;
        return VKD3D_OK;
    }
    else
    {
        if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
            return ret;

        *patch_vertex_count = 0;

        while (!shader_sm4_is_end(parser.data, &parser.ptr))
        {
            shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

            if (instruction.handler_idx == VKD3DSIH_INVALID)
            {
                WARN("Encountered unrecognized or invalid instruction.\n");
                vkd3d_shader_parser_destroy(&parser);
                return VKD3D_ERROR_INVALID_ARGUMENT;
            }

            if (instruction.handler_idx == VKD3DSIH_DCL_INPUT_CONTROL_POINT_COUNT)
            {
                *patch_vertex_count = instruction.declaration.count;
                break;
            }
        }

        vkd3d_shader_parser_destroy(&parser);
        return VKD3D_OK;
    }
}

int vkd3d_shader_scan_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_shader_parser parser;
    int ret;

    TRACE("dxbc {%p, %zu}, scan_info %p.\n", dxbc->code, dxbc->size, scan_info);

    if (shader_is_dxil(dxbc->code, dxbc->size))
    {
        /* There is nothing interesting to scan. DXIL does this internally. */
        return VKD3D_OK;
    }
    else
    {
        if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
            return ret;

        while (!shader_sm4_is_end(parser.data, &parser.ptr))
        {
            shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

            if (instruction.handler_idx == VKD3DSIH_INVALID)
            {
                WARN("Encountered unrecognized or invalid instruction.\n");
                vkd3d_shader_parser_destroy(&parser);
                return VKD3D_ERROR_INVALID_ARGUMENT;
            }

            vkd3d_shader_scan_instruction(scan_info, &instruction);
        }

        vkd3d_shader_parser_destroy(&parser);
        return VKD3D_OK;
    }
}

void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *shader_code)
{
    if (!shader_code)
        return;

    vkd3d_free((void *)shader_code->code);
}

static void vkd3d_shader_free_root_signature_v_1_0(struct vkd3d_root_signature_desc *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->parameter_count; ++i)
    {
        const struct vkd3d_root_parameter *parameter = &root_signature->parameters[i];

        if (parameter->parameter_type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->descriptor_table.descriptor_ranges);
    }
    vkd3d_free((void *)root_signature->parameters);
    vkd3d_free((void *)root_signature->static_samplers);

    memset(root_signature, 0, sizeof(*root_signature));
}

static void vkd3d_shader_free_root_signature_v_1_1(struct vkd3d_root_signature_desc1 *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->parameter_count; ++i)
    {
        const struct vkd3d_root_parameter1 *parameter = &root_signature->parameters[i];

        if (parameter->parameter_type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->descriptor_table.descriptor_ranges);
    }
    vkd3d_free((void *)root_signature->parameters);
    vkd3d_free((void *)root_signature->static_samplers);

    memset(root_signature, 0, sizeof(*root_signature));
}

void vkd3d_shader_free_root_signature(struct vkd3d_versioned_root_signature_desc *desc)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        vkd3d_shader_free_root_signature_v_1_0(&desc->v_1_0);
    }
    else if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_1)
    {
        vkd3d_shader_free_root_signature_v_1_1(&desc->v_1_1);
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

vkd3d_shader_hash_t vkd3d_shader_hash(const struct vkd3d_shader_code *shader)
{
    vkd3d_shader_hash_t h = 0xcbf29ce484222325ull;
    const uint8_t *code = shader->code;
    size_t i, n;

    for (i = 0, n = shader->size; i < n; i++)
        h = (h * 0x100000001b3ull) ^ code[i];

    return h;
}
