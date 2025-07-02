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
#include "vkd3d_string.h"

#include "vkd3d_platform.h"
#include "vkd3d_threads.h"

#include <stdio.h>
#include <inttypes.h>

#include "vkd3d_dxcapi.h"

static void vkd3d_shader_dump_blob(const char *path, vkd3d_shader_hash_t hash, const void *data, size_t size, const char *ext)
{
    char filename[1024];
    FILE *f;

    snprintf(filename, ARRAY_SIZE(filename), "%s/%016"PRIx64".%s", path, hash, ext);

    INFO("Dumping blob to %s.\n", filename);

    /* Exclusive open to avoid multiple threads spamming out the same shader module, and avoids race condition. */
    if ((f = fopen(filename, "wbx")))
    {
        if (fwrite(data, 1, size, f) != size)
            ERR("Failed to write shader to %s.\n", filename);
        if (fclose(f))
            ERR("Failed to close stream %s.\n", filename);
    }
}

static bool vkd3d_shader_replace_path(const char *filename, vkd3d_shader_hash_t hash, const void **data, size_t *size)
{
    void *buffer = NULL;
    FILE *f = NULL;
    size_t len;

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
    INFO("Overriding shader hash %016"PRIx64" with alternative SPIR-V module from %s!\n", hash, filename);
    fclose(f);
    return true;

err:
    if (f)
        fclose(f);
    vkd3d_free(buffer);
    return false;
}

bool vkd3d_shader_replace(vkd3d_shader_hash_t hash, const void **data, size_t *size)
{
    static bool enabled = true;
    char path[VKD3D_PATH_MAX];
    char filename[1024];

    if (!enabled)
        return false;

    if (!vkd3d_get_env_var("VKD3D_SHADER_OVERRIDE", path, sizeof(path)))
    {
        enabled = false;
        return false;
    }

    snprintf(filename, ARRAY_SIZE(filename), "%s/%016"PRIx64".spv", path, hash);
    return vkd3d_shader_replace_path(filename, hash, data, size);
}

bool vkd3d_shader_replace_export(vkd3d_shader_hash_t hash, const void **data, size_t *size, const char *export)
{
    static bool enabled = true;
    char path[VKD3D_PATH_MAX];
    char filename[1024];

    if (!enabled)
        return false;

    if (!vkd3d_get_env_var("VKD3D_SHADER_OVERRIDE", path, sizeof(path)))
    {
        enabled = false;
        return false;
    }

    snprintf(filename, ARRAY_SIZE(filename), "%s/%016"PRIx64".lib.%s.spv", path, hash, export);
    return vkd3d_shader_replace_path(filename, hash, data, size);
}

void vkd3d_shader_dump_shader(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader, const char *ext)
{
    static bool enabled = true;
    char path[VKD3D_PATH_MAX];

    if (!enabled)
        return;

    if (!vkd3d_get_env_var("VKD3D_SHADER_DUMP_PATH", path, sizeof(path)))
    {
        enabled = false;
        return;
    }

    vkd3d_shader_dump_blob(path, hash, shader->code, shader->size, ext);
}

void vkd3d_shader_dump_spirv_shader(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader)
{
    static bool enabled = true;
    char path[VKD3D_PATH_MAX];

    if (!enabled)
        return;

    if (!vkd3d_get_env_var("VKD3D_SHADER_DUMP_PATH", path, sizeof(path)))
    {
        enabled = false;
        return;
    }

    vkd3d_shader_dump_blob(path, hash, shader->code, shader->size, "spv");
}

void vkd3d_shader_dump_spirv_shader_export(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader,
        const char *export)
{
    static bool enabled = true;
    char path[VKD3D_PATH_MAX];
    char tag[1024];

    if (!enabled)
        return;

    if (!vkd3d_get_env_var("VKD3D_SHADER_DUMP_PATH", path, sizeof(path)))
    {
        enabled = false;
        return;
    }

    snprintf(tag, sizeof(tag), "lib.%s.spv", export);
    vkd3d_shader_dump_blob(path, hash, shader->code, shader->size, tag);
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
    unsigned required_components;
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
    const struct vkd3d_shader_scan_entry *e;
    struct vkd3d_shader_scan_key key;

    key.register_type = type;
    key.register_id = id;

    e = (const struct vkd3d_shader_scan_entry *)hash_map_find(&scan_info->register_map, &key);
    return e ? e->flags : 0u;
}

unsigned int vkd3d_shader_scan_get_idxtemp_components(const struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    const struct vkd3d_shader_scan_entry *e;
    struct vkd3d_shader_scan_key key;

    key.register_type = reg->type;
    key.register_id = reg->idx[0].offset;

    e = (const struct vkd3d_shader_scan_entry *)hash_map_find(&scan_info->register_map, &key);
    return e ? e->required_components : 4u;
}

static void vkd3d_shader_scan_set_register_flags(struct vkd3d_shader_scan_info *scan_info,
        enum vkd3d_shader_register_type type, unsigned int id, unsigned int flags)
{
    struct vkd3d_shader_scan_entry entry;
    struct vkd3d_shader_scan_entry *e;
    struct vkd3d_shader_scan_key key;

    key.register_type = type;
    key.register_id = id;

    if ((e = (struct vkd3d_shader_scan_entry *)hash_map_find(&scan_info->register_map, &key)))
    {
        e->flags |= flags;
    }
    else
    {
        entry.key = key;
        entry.flags = flags;
        entry.required_components = 0;
        hash_map_insert(&scan_info->register_map, &key, &entry.entry);
    }
}

static void vkd3d_shader_scan_record_idxtemp_components(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg, unsigned int required_components)
{
    struct vkd3d_shader_scan_entry entry;
    struct vkd3d_shader_scan_entry *e;
    struct vkd3d_shader_scan_key key;

    key.register_type = reg->type;
    key.register_id = reg->idx[0].offset;

    if ((e = (struct vkd3d_shader_scan_entry *)hash_map_find(&scan_info->register_map, &key)))
    {
        e->required_components = max(required_components, e->required_components);
    }
    else
    {
        entry.key = key;
        entry.flags = 0;
        entry.required_components = required_components;
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
    hash_map_free(&scan_info->register_map);
}

static int vkd3d_shader_validate_shader_type(enum vkd3d_shader_type type, VkShaderStageFlagBits stages)
{
    static const VkShaderStageFlagBits table[VKD3D_SHADER_TYPE_COUNT] = {
        VK_SHADER_STAGE_FRAGMENT_BIT,
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_COMPUTE_BIT,
    };

    if (type >= VKD3D_SHADER_TYPE_COUNT)
        return VKD3D_ERROR_INVALID_ARGUMENT;

    if (table[type] != stages)
    {
        ERR("Expected VkShaderStage #%x, but got VkShaderStage #%x.\n", stages, table[type]);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    return 0;
}

#ifdef VKD3D_ENABLE_DXILCONV
static DxcCreateInstanceProc vkd3d_dxilconv_instance_proc;

static void dxilconv_init_once(void)
{
    vkd3d_module_t module = vkd3d_dlopen("dxilconv.dll");
    if (module)
        vkd3d_dxilconv_instance_proc = vkd3d_dlsym(module, "DxcCreateInstance");

    if (vkd3d_dxilconv_instance_proc)
        INFO("Found dxilconv.dll. Will use that for DXBC.\n");
    else
        INFO("Did not find dxilconv.dll. Using built-in DXBC implementation.\n");
}

static IDxbcConverter *vkd3d_shader_compiler_create_dxbc_converter(void)
{
    static pthread_once_t once_key = PTHREAD_ONCE_INIT;
    IDxbcConverter *iface;

    pthread_once(&once_key, dxilconv_init_once);
    if (!vkd3d_dxilconv_instance_proc)
        return NULL;

    if (FAILED(vkd3d_dxilconv_instance_proc(&CLSID_DxbcConverter, &IID_IDxbcConverter, (void **)&iface)))
        return NULL;

    return iface;
}
#endif

int vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv,
        struct vkd3d_shader_code_debug *spirv_debug,
        unsigned int compiler_options,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compile_args)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_dxbc_compiler *spirv_compiler;
    struct vkd3d_shader_scan_info scan_info;
    struct vkd3d_shader_parser parser;
    vkd3d_shader_hash_t hash;
    bool is_dxil;
    int ret;

    TRACE("dxbc {%p, %zu}, spirv %p, compiler_options %#x, shader_interface_info %p, compile_args %p.\n",
            dxbc->code, dxbc->size, spirv, compiler_options, shader_interface_info, compile_args);

    if ((ret = vkd3d_shader_validate_compile_args(compile_args)) < 0)
        return ret;

    is_dxil = shader_is_dxil(dxbc->code, dxbc->size);

#ifdef VKD3D_ENABLE_DXILCONV
    if (!is_dxil)
    {
        IDxbcConverter *conv = vkd3d_shader_compiler_create_dxbc_converter();
        if (conv)
        {
            struct vkd3d_shader_code converted;
            UINT32 dxil_size;
            int ret = -1;
            void *dxil;

            if (SUCCEEDED(IDxbcConverter_Convert(conv, dxbc->code, dxbc->size, NULL, &dxil, &dxil_size, NULL)))
            {
                converted.code = dxil;
                converted.size = dxil_size;
                spirv->meta.hash = vkd3d_shader_hash(dxbc);
                ret = vkd3d_shader_compile_dxil(&converted, spirv, spirv_debug, shader_interface_info, compile_args);
                CoTaskMemFree(dxil);
            }
            IDxbcConverter_Release(conv);

            if (ret == 0)
                return ret;
        }
    }
#endif

    /* DXIL is handled externally through dxil-spirv. */
    if (is_dxil)
    {
        spirv->meta.hash = 0;
        return vkd3d_shader_compile_dxil(dxbc, spirv, spirv_debug, shader_interface_info, compile_args);
    }

    memset(&spirv->meta, 0, sizeof(spirv->meta));

    hash = vkd3d_shader_hash(dxbc);
    spirv->meta.hash = hash;
    if (vkd3d_shader_replace(hash, &spirv->code, &spirv->size))
    {
        spirv->meta.flags |= VKD3D_SHADER_META_FLAG_REPLACED;
        return VKD3D_OK;
    }

    vkd3d_shader_dump_shader(hash, dxbc, "dxbc");

    vkd3d_shader_scan_init(&scan_info);

    if ((ret = vkd3d_shader_scan_dxbc(dxbc, &scan_info)) < 0)
    {
        vkd3d_shader_scan_destroy(&scan_info);
        return ret;
    }

    spirv->meta.patch_vertex_count = scan_info.patch_vertex_count;

    if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
    {
        vkd3d_shader_scan_destroy(&scan_info);
        return ret;
    }

    if (shader_interface_info)
    {
        if ((ret = vkd3d_shader_validate_shader_type(parser.shader_version.type, shader_interface_info->stage)) < 0)
        {
            vkd3d_shader_scan_destroy(&scan_info);
            vkd3d_shader_parser_destroy(&parser);
            return ret;
        }
    }

    if (!(spirv_compiler = vkd3d_dxbc_compiler_create(&parser.shader_version,
            &parser.shader_desc, compiler_options, shader_interface_info, compile_args, &scan_info,
            spirv->meta.hash)))
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

    if (spirv_debug)
        memset(spirv_debug, 0, sizeof(*spirv_debug));

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

static bool vkd3d_shader_instruction_is_uav_write(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return (VKD3DSIH_ATOMIC_AND <= handler_idx && handler_idx <= VKD3DSIH_ATOMIC_XOR)
            || (VKD3DSIH_IMM_ATOMIC_ALLOC <= handler_idx && handler_idx <= VKD3DSIH_IMM_ATOMIC_XOR)
            || handler_idx == VKD3DSIH_STORE_UAV_TYPED
            || handler_idx == VKD3DSIH_STORE_RAW
            || handler_idx == VKD3DSIH_STORE_STRUCTURED;
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

static void vkd3d_shader_scan_record_uav_write(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    vkd3d_shader_scan_set_register_flags(scan_info, VKD3DSPR_UAV,
            reg->idx[0].offset, VKD3D_SHADER_UAV_FLAG_WRITE_ACCESS);
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
    scan_info->has_side_effects = true;
    scan_info->has_uav_counter = true;
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

static void vkd3d_shader_scan_output_declaration(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_instruction *instruction)
{
    switch (instruction->declaration.dst.reg.type)
    {
        case VKD3DSPR_DEPTHOUT:
        case VKD3DSPR_DEPTHOUTLE:
        case VKD3DSPR_DEPTHOUTGE:
        case VKD3DSPR_STENCILREFOUT:
        case VKD3DSPR_SAMPLEMASK:
            scan_info->needs_late_zs = true;
            break;

        default:
            break;
    }
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
        case VKD3DSIH_DCL_OUTPUT:
            vkd3d_shader_scan_output_declaration(scan_info, instruction);
            break;
        case VKD3DSIH_DISCARD:
            scan_info->discards = true;
            break;
        case VKD3DSIH_DCL_GLOBAL_FLAGS:
            if (instruction->flags & VKD3DSGF_FORCE_EARLY_DEPTH_STENCIL)
                scan_info->early_fragment_tests = true;
            break;
        case VKD3DSIH_DCL_INPUT_CONTROL_POINT_COUNT:
            scan_info->patch_vertex_count = instruction->declaration.count;
            break;
        case VKD3DSIH_DCL_UAV_RAW:
        case VKD3DSIH_DCL_UAV_STRUCTURED:
        case VKD3DSIH_DCL_UAV_TYPED:
            /* See test_memory_model_uav_coherent_thread_group() for details. */
            if (instruction->flags & VKD3DSUF_GLOBALLY_COHERENT)
                scan_info->declares_globally_coherent_uav = true;
            if (instruction->flags & VKD3DSUF_RASTERIZER_ORDERED)
                scan_info->requires_rov = true;
            break;
        case VKD3DSIH_SYNC:
            /* See test_memory_model_uav_coherent_thread_group() for details. */
            if (instruction->flags & (VKD3DSSF_UAV_MEMORY_LOCAL | VKD3DSSF_UAV_MEMORY_GLOBAL))
                scan_info->requires_thread_group_uav_coherency = true;
            break;
        default:
            break;
    }

    /* If we do nothing, we will have to assume that IDXTEMP is an array of vec4.
     * This is problematic for performance if shader only accesses the first 1, 2 or 3 components.
     * The dcl_indexableTemp instruction specifies number of components but FXC does not seem to
     * care, so we have to analyze write masks instead. */
    for (i = 0; i < instruction->dst_count; ++i)
    {
        if (instruction->dst[i].reg.type == VKD3DSPR_IDXTEMP)
        {
            unsigned int write_mask, required_components;
            write_mask = instruction->dst[i].write_mask;
            write_mask |= write_mask >> 2;
            write_mask |= write_mask >> 1;
            required_components = vkd3d_write_mask_component_count(write_mask);
            vkd3d_shader_scan_record_idxtemp_components(scan_info,
                    &instruction->dst[i].reg, required_components);
        }
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

    if (vkd3d_shader_instruction_is_uav_write(instruction))
    {
        scan_info->has_side_effects = true;
        for (i = 0; i < instruction->dst_count; ++i)
            if (instruction->dst[i].reg.type == VKD3DSPR_UAV)
                vkd3d_shader_scan_record_uav_write(scan_info, &instruction->dst[i].reg);
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

void vkd3d_shader_free_shader_code_debug(struct vkd3d_shader_code_debug *shader_code)
{
    if (!shader_code)
        return;

    vkd3d_free((void *)shader_code->debug_entry_point_name);
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

static void vkd3d_shader_free_root_signature_v_1_2(struct vkd3d_root_signature_desc2 *root_signature)
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
    else if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_2)
    {
        vkd3d_shader_free_root_signature_v_1_2(&desc->v_1_2);
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

int vkd3d_shader_parse_output_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature)
{
    TRACE("dxbc {%p, %zu}, signature %p.\n", dxbc->code, dxbc->size, signature);

    return shader_parse_output_signature(dxbc->code, dxbc->size, signature);
}

int vkd3d_shader_parse_patch_constant_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature)
{
    TRACE("dxbc {%p, %zu}, signature %p.\n", dxbc->code, dxbc->size, signature);

    return shader_parse_patch_constant_signature(dxbc->code, dxbc->size, signature);
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
    vkd3d_shader_hash_t h = hash_fnv1_init();
    const uint8_t *code = shader->code;
    size_t i, n;

    for (i = 0, n = shader->size; i < n; i++)
        h = hash_fnv1_iterate_u8(h, code[i]);

    return h;
}

uint32_t vkd3d_shader_compile_arguments_select_quirks(
        const struct vkd3d_shader_compile_arguments *compile_args, vkd3d_shader_hash_t shader_hash)
{
    unsigned int i;
    if (compile_args && compile_args->quirks)
    {
        for (i = 0; i < compile_args->quirks->num_hashes; i++)
            if (compile_args->quirks->hashes[i].shader_hash == shader_hash)
                return compile_args->quirks->hashes[i].quirks | compile_args->quirks->global_quirks;
        return compile_args->quirks->default_quirks | compile_args->quirks->global_quirks;
    }
    else
        return 0;
}

uint64_t vkd3d_shader_get_revision(void)
{
    /* This is meant to be bumped every time a change is made to the shader compiler.
     * Might get nuked later ...
     * It's not immediately useful for invalidating pipeline caches, since that would mostly be covered
     * by vkd3d-proton Git hash. */
#ifdef VKD3D_ENABLE_DXILCONV
    dxilconv_init_once();
    return vkd3d_dxilconv_instance_proc ? 2 : 1;
#else
    return 1;
#endif
}

struct vkd3d_shader_stage_io_entry *vkd3d_shader_stage_io_map_append(struct vkd3d_shader_stage_io_map *map,
        const char *semantic_name, unsigned int semantic_index)
{
    struct vkd3d_shader_stage_io_entry *e;

    if (vkd3d_shader_stage_io_map_find(map, semantic_name, semantic_index))
        return NULL;

    if (!vkd3d_array_reserve((void **)&map->entries, &map->entries_size,
            map->entry_count + 1, sizeof(*map->entries)))
        return NULL;

    e = &map->entries[map->entry_count++];
    e->semantic_name = vkd3d_strdup(semantic_name);
    e->semantic_index = semantic_index;
    return e;
}

const struct vkd3d_shader_stage_io_entry *vkd3d_shader_stage_io_map_find(const struct vkd3d_shader_stage_io_map *map,
        const char *semantic_name, unsigned int semantic_index)
{
    unsigned int i;

    for (i = 0; i < map->entry_count; i++)
    {
        struct vkd3d_shader_stage_io_entry *e = &map->entries[i];

        if (!strcmp(e->semantic_name, semantic_name) && e->semantic_index == semantic_index)
            return e;
    }

    return NULL;
}

void vkd3d_shader_stage_io_map_free(struct vkd3d_shader_stage_io_map *map)
{
    unsigned int i;

    for (i = 0; i < map->entry_count; i++)
        vkd3d_free((void *)map->entries[i].semantic_name);

    vkd3d_free(map->entries);
    memset(map, 0, sizeof(*map));
}

static int vkd3d_shader_parse_root_signature_for_version(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        enum vkd3d_root_signature_version target_version,
        bool raw_payload,
        vkd3d_shader_hash_t *compatibility_hash)
{
    struct vkd3d_versioned_root_signature_desc desc, converted_desc;
    int ret;

    if (raw_payload)
    {
        if ((ret = vkd3d_shader_parse_root_signature_raw(dxbc->code, dxbc->size, &desc, compatibility_hash)) < 0)
        {
            WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
            return ret;
        }
    }
    else
    {
        if ((ret = vkd3d_shader_parse_root_signature(dxbc, &desc, compatibility_hash)) < 0)
        {
            WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
            return ret;
        }
    }

    if (desc.version == target_version)
    {
        *out_desc = desc;
    }
    else
    {
        ret = vkd3d_shader_convert_root_signature(&converted_desc, target_version, &desc);
        vkd3d_shader_free_root_signature(&desc);
        if (ret < 0)
        {
            WARN("Failed to convert from version %#x, vkd3d result %d.\n", desc.version, ret);
            return ret;
        }

        *out_desc = converted_desc;
    }

    return ret;
}

int vkd3d_shader_parse_root_signature_v_1_0(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash)
{
    return vkd3d_shader_parse_root_signature_for_version(dxbc, out_desc, VKD3D_ROOT_SIGNATURE_VERSION_1_0, false,
            compatibility_hash);
}

int vkd3d_shader_parse_root_signature_v_1_2(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash)
{
    return vkd3d_shader_parse_root_signature_for_version(dxbc, out_desc, VKD3D_ROOT_SIGNATURE_VERSION_1_2, false,
            compatibility_hash);
}

int vkd3d_shader_parse_root_signature_v_1_2_from_raw_payload(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash)
{
    return vkd3d_shader_parse_root_signature_for_version(dxbc, out_desc, VKD3D_ROOT_SIGNATURE_VERSION_1_2, true,
            compatibility_hash);
}

vkd3d_shader_hash_t vkd3d_root_signature_v_1_2_compute_layout_compat_hash(
        const struct vkd3d_root_signature_desc2 *desc)
{
    vkd3d_shader_hash_t hash = hash_fnv1_init();
    uint32_t i;

    hash = hash_fnv1_iterate_u32(hash, desc->static_sampler_count);
    hash = hash_fnv1_iterate_u32(hash, desc->parameter_count);
    hash = hash_fnv1_iterate_u32(hash, desc->flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    for (i = 0; i < desc->parameter_count; i++)
    {
        hash = hash_fnv1_iterate_u32(hash, desc->parameters[i].parameter_type);
        if (desc->parameters[i].parameter_type == VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            hash = hash_fnv1_iterate_u32(hash, desc->parameters[i].constants.value_count);
        else
            hash = hash_fnv1_iterate_u32(hash, 0);
    }

    for (i = 0; i < desc->static_sampler_count; i++)
    {
        const struct vkd3d_static_sampler_desc1 *sampler = &desc->static_samplers[i];
        /* Ignore space / register since those don't affect VkPipelineLayout. */
        hash = hash_fnv1_iterate_u32(hash, sampler->flags);
        hash = hash_fnv1_iterate_u32(hash, sampler->shader_visibility);
        hash = hash_fnv1_iterate_u32(hash, sampler->max_anisotropy);
        hash = hash_fnv1_iterate_u32(hash, sampler->border_color);
        hash = hash_fnv1_iterate_u32(hash, sampler->comparison_func);
        hash = hash_fnv1_iterate_u32(hash, sampler->address_u);
        hash = hash_fnv1_iterate_u32(hash, sampler->address_v);
        hash = hash_fnv1_iterate_u32(hash, sampler->address_w);
        hash = hash_fnv1_iterate_u32(hash, sampler->filter);
        hash = hash_fnv1_iterate_f32(hash, sampler->min_lod);
        hash = hash_fnv1_iterate_f32(hash, sampler->max_lod);
        hash = hash_fnv1_iterate_f32(hash, sampler->mip_lod_bias);
    }

    return hash;
}

bool vkd3d_shader_hash_range_parse_line(char *line,
        vkd3d_shader_hash_t *lo, vkd3d_shader_hash_t *hi,
        char **trail)
{
    vkd3d_shader_hash_t lo_hash;
    vkd3d_shader_hash_t hi_hash;
    char *old_end_ptr;
    char *end_ptr;

    /* Look for either a single number, or lohash-hihash format. */
    if (!isalnum(*line))
        return false;
    lo_hash = strtoull(line, &end_ptr, 16);

    while (*end_ptr != '\0' && !isalnum(*end_ptr))
        end_ptr++;

    old_end_ptr = end_ptr;
    hi_hash = strtoull(end_ptr, &end_ptr, 16);

    /* If we didn't fully consume a hex number here, back up. */
    if (*end_ptr != '\0' && *end_ptr != '\n' && *end_ptr != ' ')
    {
        end_ptr = old_end_ptr;
        hi_hash = 0;
    }

    while (*end_ptr != '\0' && !isalpha(*end_ptr))
        end_ptr++;

    if (!hi_hash)
        hi_hash = lo_hash;

    *lo = lo_hash;
    *hi = hi_hash;
    *trail = end_ptr;

    if (*end_ptr != '\0')
    {
        char *stray_newline = end_ptr + (strlen(end_ptr) - 1);
        if (*stray_newline == '\n')
            *stray_newline = '\0';
    }

    return true;
}
