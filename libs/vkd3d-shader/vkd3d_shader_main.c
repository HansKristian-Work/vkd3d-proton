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
#include "vkd3d_threads.h"

#include "vkd3d_platform.h"

#include <stdio.h>
#include <inttypes.h>

#include "spirv/unified1/spirv.h"

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

int vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv,
        struct vkd3d_shader_code_debug *spirv_debug,
        unsigned int compiler_options,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compile_args)
{
    bool is_dxil;
    int ret;

    TRACE("dxbc {%p, %zu}, spirv %p, compiler_options %#x, shader_interface_info %p, compile_args %p.\n",
            dxbc->code, dxbc->size, spirv, compiler_options, shader_interface_info, compile_args);

    if ((ret = vkd3d_shader_validate_compile_args(compile_args)) < 0)
        return ret;

    is_dxil = shader_is_dxil(dxbc->code, dxbc->size);
    /* Shader models 4 through 6.x are handled externally through dxil-spirv. */
    spirv->meta.hash = 0;
    return vkd3d_shader_compile_dxil(dxbc, spirv, spirv_debug, shader_interface_info, compile_args, is_dxil);
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

struct vkd3d_shader_quirk_entry
{
    vkd3d_shader_hash_t lo;
    vkd3d_shader_hash_t hi;
    uint32_t flags;
};

static struct vkd3d_shader_quirk_entry *vkd3d_shader_quirk_entries;
size_t vkd3d_shader_quirk_entry_count;

#define ENTRY(x) { #x, VKD3D_SHADER_QUIRK_ ## x }
static const struct vkd3d_shader_quirk_mapping
{
    const char *name;
    enum vkd3d_shader_quirk quirk;
} vkd3d_shader_quirk_mappings[] = {
    ENTRY(FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW),
    ENTRY(FORCE_TGSM_BARRIERS),
    ENTRY(INVARIANT_POSITION),
    ENTRY(FORCE_NOCONTRACT_MATH),
    ENTRY(LIMIT_TESS_FACTORS_32),
    ENTRY(LIMIT_TESS_FACTORS_16),
    ENTRY(LIMIT_TESS_FACTORS_8),
    ENTRY(LIMIT_TESS_FACTORS_4),
    ENTRY(FORCE_SUBGROUP_SIZE_1),
    ENTRY(FORCE_MAX_WAVE32),
    ENTRY(FORCE_MIN16_AS_32BIT),
    ENTRY(REWRITE_GRAD_TO_BIAS),
    ENTRY(FORCE_LOOP),
    ENTRY(DESCRIPTOR_HEAP_ROBUSTNESS),
    ENTRY(DISABLE_OPTIMIZATIONS),
    ENTRY(FORCE_NOCONTRACT_MATH_VS),
    ENTRY(FORCE_DEVICE_MEMORY_BARRIER_THREAD_GROUP_COHERENCY),
    ENTRY(ASSUME_BROKEN_SUB_8x8_CUBE_MIPS),
    ENTRY(FORCE_ROBUST_PHYSICAL_CBV_LOAD_FORWARDING),
    ENTRY(AGGRESSIVE_NONUNIFORM),
    ENTRY(HOIST_DERIVATIVES),
    ENTRY(FORCE_MIN_WAVE32),
    ENTRY(PROMOTE_GROUP_TO_DEVICE_MEMORY_BARRIER),
    ENTRY(FORCE_GRAPHICS_BARRIER_BEFORE_RENDER_PASS),
    ENTRY(FIXUP_LOOP_HEADER_UNDEF_PHIS),
    ENTRY(FIXUP_RSQRT_INF_NAN),
};
#undef ENTRY

static void vkd3d_shader_init_quirk_table(void)
{
    struct vkd3d_shader_quirk_entry entry;
    size_t size = 0;
    char env[128];
    char *trail;
    FILE *file;
    size_t i;

    if (!vkd3d_get_env_var("VKD3D_SHADER_QUIRKS", env, sizeof(env)))
        return;

    file = fopen(env, "r");
    if (!file)
    {
        INFO("Failed to open VKD3D_SHADER_QUIRKS file \"%s\".\n", env);
        return;
    }

    while (fgets(env, sizeof(env), file))
    {
        if (!vkd3d_shader_hash_range_parse_line(env, &entry.lo, &entry.hi, &trail))
            continue;

        if (*trail == '\0')
            continue;

        for (i = 0; i < ARRAY_SIZE(vkd3d_shader_quirk_mappings); i++)
        {
            if (strcmp(trail, vkd3d_shader_quirk_mappings[i].name) == 0)
            {
                entry.flags = vkd3d_shader_quirk_mappings[i].quirk;
                INFO("Parsed shader quirk entry: [%016"PRIx64", %016"PRIx64"] -> %s\n",
                        entry.lo, entry.hi, trail);
                break;
            }
        }

        if (i == ARRAY_SIZE(vkd3d_shader_quirk_mappings))
        {
            INFO("Parsed shader quirk entry: [%016"PRIx64", %016"PRIx64"], but no quirk for %s was found.\n",
                    entry.lo, entry.hi, trail);
        }

        vkd3d_array_reserve((void **)&vkd3d_shader_quirk_entries, &size,
                vkd3d_shader_quirk_entry_count + 1, sizeof(*vkd3d_shader_quirk_entries));
        vkd3d_shader_quirk_entries[vkd3d_shader_quirk_entry_count++] = entry;
    }

    fclose(file);
}

static pthread_once_t vkd3d_shader_quirk_once = PTHREAD_ONCE_INIT;

uint32_t vkd3d_shader_compile_arguments_select_quirks(
        const struct vkd3d_shader_compile_arguments *compile_args, vkd3d_shader_hash_t shader_hash)
{
    uint32_t quirks = 0;
    unsigned int i;

    pthread_once(&vkd3d_shader_quirk_once, vkd3d_shader_init_quirk_table);

    for (i = 0; i < vkd3d_shader_quirk_entry_count; i++)
    {
        if (vkd3d_shader_quirk_entries[i].lo <= shader_hash && vkd3d_shader_quirk_entries[i].hi >= shader_hash)
        {
            quirks |= vkd3d_shader_quirk_entries[i].flags;
            INFO("Adding shader quirks #%x for hash %016"PRIx64".\n",
                    vkd3d_shader_quirk_entries[i].flags, shader_hash);
        }
    }

    if (compile_args && compile_args->quirks)
    {
        for (i = 0; i < compile_args->quirks->num_hashes; i++)
            if (compile_args->quirks->hashes[i].shader_hash == shader_hash)
                return quirks | compile_args->quirks->hashes[i].quirks | compile_args->quirks->global_quirks;
        return quirks | compile_args->quirks->default_quirks | compile_args->quirks->global_quirks;
    }
    else
        return quirks;
}

uint64_t vkd3d_shader_get_revision(void)
{
    uint64_t quirk_hash = 0;
    size_t i;

    pthread_once(&vkd3d_shader_quirk_once, vkd3d_shader_init_quirk_table);

    if (vkd3d_shader_quirk_entry_count)
    {
        quirk_hash = hash_fnv1_init();
        for (i = 0; i < vkd3d_shader_quirk_entry_count; i++)
        {
            quirk_hash = hash_fnv1_iterate_u64(quirk_hash, vkd3d_shader_quirk_entries[i].lo);
            quirk_hash = hash_fnv1_iterate_u64(quirk_hash, vkd3d_shader_quirk_entries[i].hi);
            quirk_hash = hash_fnv1_iterate_u32(quirk_hash, vkd3d_shader_quirk_entries[i].flags);
        }
    }

    /* This is meant to be bumped every time a change is made to the shader compiler.
     * Might get nuked later ...
     * It's not immediately useful for invalidating pipeline caches, since that would mostly be covered
     * by vkd3d-proton Git hash. */
    return quirk_hash ^ 1;
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

void vkd3d_shader_extract_feature_meta(struct vkd3d_shader_code *code)
{
    SpvExecutionModel execution_model = SpvExecutionModelMax;
    size_t spirv_words = code->size / sizeof(uint32_t);
    unsigned int i, tracked_builtin_count = 0;
    const uint32_t *spirv = code->code;
    SpvExecutionMode execution_mode;
    SpvStorageClass storage_class;
    SpvCapability capability;
    SpvDecoration decoration;
    SpvBuiltIn builtin;
    size_t offset = 5;
    uint32_t meta = 0;
    uint32_t var_id;

    /* This array must be large enough to hold all variable IDs that may
     * be decorated with relevant built-ins in a valid SPIR-V module */
    struct vkd3d_tracked_builtin
    {
        uint32_t var_id;
        SpvBuiltIn builtin;
    }
    tracked_builtins[2];

    while (offset < spirv_words)
    {
        unsigned count = (spirv[offset] >> 16) & 0xffff;
        SpvOp op = spirv[offset] & 0xffff;

        if (count == 0 || offset + count > spirv_words)
            break;

        if (op == SpvOpCapability && count == 2)
        {
            capability = spirv[offset + 1];
            switch (capability)
            {
                case SpvCapabilityShaderViewportIndexLayerEXT:
                    meta |= VKD3D_SHADER_META_FLAG_USES_SHADER_VIEWPORT_INDEX_LAYER;
                    break;

                case SpvCapabilitySparseResidency:
                    meta |= VKD3D_SHADER_META_FLAG_USES_SPARSE_RESIDENCY;
                    break;

                case SpvCapabilityFragmentFullyCoveredEXT:
                    meta |= VKD3D_SHADER_META_FLAG_USES_FRAGMENT_FULLY_COVERED;
                    break;

                case SpvCapabilityInt64:
                    meta |= VKD3D_SHADER_META_FLAG_USES_INT64;
                    break;

                case SpvCapabilityStencilExportEXT:
                    meta |= VKD3D_SHADER_META_FLAG_USES_STENCIL_EXPORT;
                    break;

                case SpvCapabilityFloat64:
                    meta |= VKD3D_SHADER_META_FLAG_USES_FP64;
                    break;

                case SpvCapabilityStorageUniform16:
                case SpvCapabilityStorageUniformBufferBlock16:
                case SpvCapabilityStorageInputOutput16:
                case SpvCapabilityFloat16:
                    /* Int16 is hard requirement, and should not affect this check. */
                    meta |= VKD3D_SHADER_META_FLAG_USES_NATIVE_16BIT_OPERATIONS;
                    break;

                case SpvCapabilityInt64Atomics:
                    meta |= VKD3D_SHADER_META_FLAG_USES_INT64_ATOMICS;
                    break;

                case SpvCapabilityInt64ImageEXT:
                    meta |= VKD3D_SHADER_META_FLAG_USES_INT64_ATOMICS_IMAGE;
                    break;

                case SpvCapabilityFragmentBarycentricKHR:
                    meta |= VKD3D_SHADER_META_FLAG_USES_FRAGMENT_BARYCENTRIC;
                    break;

                case SpvCapabilitySampleRateShading:
                    meta |= VKD3D_SHADER_META_FLAG_USES_SAMPLE_RATE_SHADING;
                    break;

                case SpvCapabilityFragmentShaderPixelInterlockEXT:
                case SpvCapabilityFragmentShaderSampleInterlockEXT:
                    meta |= VKD3D_SHADER_META_FLAG_USES_RASTERIZER_ORDERED_VIEWS;
                    break;

                case SpvCapabilityGroupNonUniform:
                case SpvCapabilityGroupNonUniformVote:
                case SpvCapabilityGroupNonUniformArithmetic:
                case SpvCapabilityGroupNonUniformBallot:
                case SpvCapabilityGroupNonUniformShuffle:
                case SpvCapabilityGroupNonUniformShuffleRelative:
                case SpvCapabilityGroupNonUniformClustered:
                case SpvCapabilityGroupNonUniformQuad:
                    meta |= VKD3D_SHADER_META_FLAG_USES_SUBGROUP_OPERATIONS;
                    break;

                case SpvCapabilityCooperativeMatrixKHR:
                    meta |= VKD3D_SHADER_META_FLAG_USES_COOPERATIVE_MATRIX;
                    /* Semi-correct, since we use subgroup scope. We need this for wave size fixups. */
                    meta |= VKD3D_SHADER_META_FLAG_USES_SUBGROUP_OPERATIONS;
                    break;

                case SpvCapabilityFloat8CooperativeMatrixEXT:
                    meta |= VKD3D_SHADER_META_FLAG_USES_COOPERATIVE_MATRIX_FP8;
                    break;

                default:
                    break;
            }
        }
        else if (op == SpvOpEntryPoint && count >= 2)
        {
            /* Assume only one entry point per module */
            execution_model = spirv[offset + 1];
        }
        else if (op == SpvOpExecutionMode && count == 3)
        {
            execution_mode = spirv[offset + 2];
            switch (execution_mode)
            {
                case SpvExecutionModeIsolines:
                case SpvExecutionModeOutputLineStrip:
                case SpvExecutionModeOutputLinesEXT:
                    meta |= VKD3D_SHADER_META_FLAG_EMITS_LINES;
                    break;

                case SpvExecutionModeInputPoints:
                    if (execution_model == SpvExecutionModelGeometry)
                        code->meta.gs_input_topology = (uint8_t)VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                    break;

                case SpvExecutionModeInputLines:
                    if (execution_model == SpvExecutionModelGeometry)
                        code->meta.gs_input_topology = (uint8_t)VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    break;

                case SpvExecutionModeInputLinesAdjacency:
                    if (execution_model == SpvExecutionModelGeometry)
                        code->meta.gs_input_topology = (uint8_t)VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
                    break;

                case SpvExecutionModeTriangles:
                    /* For GS, this defines the input topology since the corresponding output
                     * topology would have to be TriangleStrip instead. For TES, this actually
                     * declares the output topology. */
                    if (execution_model == SpvExecutionModelGeometry)
                        code->meta.gs_input_topology = (uint8_t)VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    else
                        meta |= VKD3D_SHADER_META_FLAG_EMITS_TRIANGLES;
                    break;

                case SpvExecutionModeInputTrianglesAdjacency:
                    if (execution_model == SpvExecutionModelGeometry)
                        code->meta.gs_input_topology = (uint8_t)VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
                    break;

                case SpvExecutionModeQuads:
                case SpvExecutionModeOutputTriangleStrip:
                case SpvExecutionModeOutputTrianglesEXT:
                    meta |= VKD3D_SHADER_META_FLAG_EMITS_TRIANGLES;
                    break;

                case SpvExecutionModeDepthGreater:
                case SpvExecutionModeDepthLess:
                case SpvExecutionModeDepthReplacing:
                case SpvExecutionModeDepthUnchanged:
                case SpvExecutionModeStencilRefReplacingEXT:
                    meta |= VKD3D_SHADER_META_FLAG_USES_DEPTH_STENCIL_WRITE;
                    break;

                case SpvExecutionModePointMode:
                    meta |= VKD3D_SHADER_META_FLAG_POINT_MODE_TESSELLATION;
                    break;

                default:
                    break;
            }
        }
        else if ((op == SpvOpDecorate && count == 4) ||
                (op == SpvOpMemberDecorate && count == 5))
        {
            unsigned int delta = op == SpvOpMemberDecorate ? 1 : 0;
            decoration = spirv[offset + delta + 2];

            if (decoration == SpvDecorationBuiltIn)
            {
                builtin = spirv[offset + delta + 3];

                if (builtin == SpvBuiltInSampleMask)
                {
                    if (tracked_builtin_count < ARRAY_SIZE(tracked_builtins))
                    {
                        struct vkd3d_tracked_builtin *entry = &tracked_builtins[tracked_builtin_count++];
                        entry->var_id = spirv[offset + 1];
                        entry->builtin = builtin;
                    }
                    else
                        ERR("Too many tracked built-in variables.\n");
                }
            }
        }
        else if (op == SpvOpVariable && count >= 4)
        {
            storage_class = spirv[offset + 3];

            if (storage_class == SpvStorageClassOutput || storage_class == SpvStorageClassInput)
            {
                var_id = spirv[offset + 2];

                for (i = 0; i < tracked_builtin_count; i++)
                {
                    const struct vkd3d_tracked_builtin *entry = &tracked_builtins[i];

                    if (entry->var_id != var_id)
                        continue;

                    switch (entry->builtin)
                    {
                        case SpvBuiltInSampleMask:
                            if (storage_class == SpvStorageClassOutput)
                                meta |= VKD3D_SHADER_META_FLAG_EXPORTS_SAMPLE_MASK;
                            break;

                        default:;
                    }
                }
            }
        }
        else if (op == SpvOpFunction)
        {
            /* We're now declaring code, so just stop parsing, there cannot be any capability ops after this. */
            break;
        }

        offset += count;
    }

    code->meta.flags |= meta;
}
