/*
 * Copyright 2024 Hans-Kristian Arntzen for Valve Corporation
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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "vkd3d_common.h"
#include "vkd3d_shader.h"

static bool read_root_signature(struct vkd3d_shader_code *shader, const char *filename)
{
    void *code;
    FILE *fd;

    memset(shader, 0, sizeof(*shader));

    if (!(fd = fopen(filename, "rb")))
        return false;

    fseek(fd, 0, SEEK_END);
    shader->size = ftell(fd);
    rewind(fd);

    if (!(code = malloc(shader->size)))
    {
        fprintf(stderr, "Out of memory.\n");
        fclose(fd);
        return false;
    }
    shader->code = code;

    if (fread(code, 1, shader->size, fd) != shader->size)
    {
        fprintf(stderr, "Could not read shader bytecode from file: '%s'.\n", filename);
        free(code);
        fclose(fd);
        return false;
    }

    fclose(fd);
    return true;
}

#define FLAG(x) { D3D12_ROOT_SIGNATURE_FLAG_##x, #x }
static const struct rs_flag_to_str
{
    D3D12_ROOT_SIGNATURE_FLAGS flag;
    const char *str;
} rs_flags[] = {
    FLAG(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),
    FLAG(DENY_VERTEX_SHADER_ROOT_ACCESS),
    FLAG(DENY_HULL_SHADER_ROOT_ACCESS),
    FLAG(DENY_DOMAIN_SHADER_ROOT_ACCESS),
    FLAG(DENY_GEOMETRY_SHADER_ROOT_ACCESS),
    FLAG(DENY_PIXEL_SHADER_ROOT_ACCESS),
    FLAG(ALLOW_STREAM_OUTPUT),
    FLAG(LOCAL_ROOT_SIGNATURE),
    FLAG(DENY_AMPLIFICATION_SHADER_ROOT_ACCESS),
    FLAG(DENY_MESH_SHADER_ROOT_ACCESS),
    FLAG(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED),
    FLAG(SAMPLER_HEAP_DIRECTLY_INDEXED),
};

#undef FLAG
#define FLAG(x) { D3D12_ROOT_DESCRIPTOR_FLAG_##x, #x }

static const struct desc_flag_to_str
{
    D3D12_ROOT_DESCRIPTOR_FLAGS flag;
    const char *str;
} desc_flags[] = {
    FLAG(DATA_VOLATILE),
    FLAG(DATA_STATIC_WHILE_SET_AT_EXECUTE),
    FLAG(DATA_STATIC),
};

#undef FLAG
#define FLAG(x) { D3D12_DESCRIPTOR_RANGE_FLAG_##x, #x }

static const struct range_flag_to_str
{
    D3D12_DESCRIPTOR_RANGE_FLAGS flag;
    const char *str;
} range_flags[] = {
    FLAG(DESCRIPTORS_VOLATILE),
    FLAG(DATA_VOLATILE),
    FLAG(DATA_STATIC_WHILE_SET_AT_EXECUTE),
    FLAG(DATA_STATIC),
    FLAG(DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS),
};
#undef FLAG

static void dump_root_signature(const struct vkd3d_root_signature_desc2 *rs)
{
    unsigned int i, j, k;

    printf("Flags:\n");
    for (i = 0; i < ARRAY_SIZE(rs_flags); i++)
        if (rs_flags[i].flag & rs->flags)
            printf("  %s\n", rs_flags[i].str);

    for (i = 0; i < rs->parameter_count; i++)
    {
        const struct vkd3d_root_parameter1 *param = &rs->parameters[i];
        const char *desc_fmt = NULL;
        printf("  [%u] (visibility #%x) || ", i, param->shader_visibility);

        switch (param->parameter_type)
        {
            case VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                printf("32BIT_CONSTANTS (b%u, space%u), Num32BitWords = %u\n",
                        param->constants.shader_register, param->constants.register_space,
                        param->constants.value_count);
                break;

            case VKD3D_ROOT_PARAMETER_TYPE_CBV:
                desc_fmt = "RootCBV (b%u, space%u)\n";
                break;

            case VKD3D_ROOT_PARAMETER_TYPE_SRV:
                desc_fmt = "RootSRV (t%u, space%u)\n";
                break;

            case VKD3D_ROOT_PARAMETER_TYPE_UAV:
                desc_fmt = "RootUAV (u%u, space%u)\n";
                break;

            case VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                printf("TABLE\n");
                for (j = 0; j < param->descriptor_table.descriptor_range_count; j++)
                {
                    const struct vkd3d_descriptor_range1 *range = &param->descriptor_table.descriptor_ranges[j];
                    const char *table_fmt = NULL;

                    switch (range->range_type)
                    {
                        case VKD3D_DESCRIPTOR_RANGE_TYPE_CBV:
                            table_fmt = "    CBV (b%u, space%u) count %u, offset %u\n";
                            break;

                        case VKD3D_DESCRIPTOR_RANGE_TYPE_SRV:
                            table_fmt = "    SRV (t%u, space%u) count %u, offset %u\n";
                            break;

                        case VKD3D_DESCRIPTOR_RANGE_TYPE_UAV:
                            table_fmt = "    UAV (u%u, space%u) count %u, offset %u\n";
                            break;

                        case VKD3D_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                            table_fmt = "    SAMPLER (s%u, space%u) count %u, offset %u\n";
                            break;

                        default:
                            break;
                    }

                    if (table_fmt)
                    {
                        printf(table_fmt,
                                range->base_shader_register, range->register_space,
                                range->descriptor_count,
                                range->descriptor_table_offset);

                        for (k = 0; k < ARRAY_SIZE(range_flags); k++)
                            if (range_flags[k].flag & range->flags)
                                printf("      %s\n", range_flags[k].str);
                    }
                }

            default:
                break;
        }

        if (desc_fmt)
        {
            printf(desc_fmt, param->descriptor.shader_register, param->descriptor.register_space);
            for (j = 0; j < ARRAY_SIZE(desc_flags); j++)
                if (desc_flags[j].flag & param->descriptor.flags)
                    printf("    %s\n", desc_flags[j].str);
        }
    }

    for (i = 0; i < rs->static_sampler_count; i++)
    {
        /* TODO: Too lazy to convert all the enums here. Fix as needed. */
        const struct vkd3d_static_sampler_desc1 *sampler = &rs->static_samplers[i];
        printf("StaticSampler (s%u, space%u):\n", sampler->shader_register, sampler->register_space);
        printf("  Filter: #%x\n", sampler->filter);
        printf("  AddrU: #%x\n", sampler->address_u);
        printf("  AddrV: #%x\n", sampler->address_v);
        printf("  AddrW: #%x\n", sampler->address_w);
        printf("  MipLODBias: %f\n", sampler->mip_lod_bias);
        printf("  MaxAniso: %u\n", sampler->max_anisotropy);
        printf("  Comparison: #%x\n", sampler->comparison_func);
        printf("  StaticBorderColor: #%x\n", sampler->border_color);
        printf("  MinLOD: %f\n", sampler->min_lod);
        printf("  MaxLOD: %f\n", sampler->max_lod);
        printf("  Visibility: #%x\n", sampler->shader_visibility);
        printf("  Flags: #%x\n", sampler->flags);
    }
}

int main(int argc, char **argv)
{
    struct vkd3d_versioned_root_signature_desc out_rs;
    vkd3d_shader_hash_t compat_hash;
    struct vkd3d_shader_code rs;
    int vr;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: vkd3d-rs-parse hash.rs\n");
        return 1;
    }

    if (!read_root_signature(&rs, argv[1]))
    {
        fprintf(stderr, "Failed to read root signature.\n");
        return 1;
    }

    vr = vkd3d_shader_parse_root_signature_v_1_2_from_raw_payload(&rs, &out_rs, &compat_hash);
    if (vr < 0)
    {
        fprintf(stderr, "Failed to parse root signature, vr %d.\n", vr);
        return 1;
    }

    printf("Root signature: %016"PRIx64"\n", compat_hash);
    dump_root_signature(&out_rs.v_1_2);

    vkd3d_shader_free_shader_code(&rs);
    vkd3d_shader_free_root_signature(&out_rs);

    return 0;
}
