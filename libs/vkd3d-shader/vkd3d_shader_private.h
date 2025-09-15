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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright 2002-2003 The wine-d3d team
 * Copyright 2002-2003 2004 Jason Edmeades
 * Copyright 2002-2003 Raphael Junqueira
 * Copyright 2005 Oliver Stieber
 * Copyright 2006 Stefan Dösinger
 * Copyright 2006-2011, 2013 Stefan Dösinger for CodeWeavers
 * Copyright 2007 Henri Verbeet
 * Copyright 2008-2009 Henri Verbeet for CodeWeavers
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

#ifndef __VKD3D_SHADER_PRIVATE_H
#define __VKD3D_SHADER_PRIVATE_H

#include "vkd3d_common.h"
#include "vkd3d_memory.h"
#include "vkd3d_shader.h"
#include "list.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

bool shader_is_dxil(const void *dxbc, size_t dxbc_length);

int shader_parse_input_signature(const void *dxbc, size_t dxbc_length,
        struct vkd3d_shader_signature *signature);
int shader_parse_output_signature(const void *dxbc, size_t dxbc_length,
        struct vkd3d_shader_signature *signature);
int shader_parse_patch_constant_signature(const void *dxbc, size_t dxbc_length,
        struct vkd3d_shader_signature *signature);

void vkd3d_compute_dxbc_checksum(const void *dxbc, size_t size, uint32_t checksum[4]);

void vkd3d_shader_dump_spirv_shader(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader);
void vkd3d_shader_dump_spirv_shader_export(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader,
        const char *export);
void vkd3d_shader_dump_shader(vkd3d_shader_hash_t hash, const struct vkd3d_shader_code *shader, const char *ext);
bool vkd3d_shader_replace(vkd3d_shader_hash_t hash, const void **data, size_t *size);
bool vkd3d_shader_replace_export(vkd3d_shader_hash_t hash, const void **data, size_t *size, const char *export);

static inline unsigned int vkd3d_shader_quirk_to_tess_factor_limit(uint32_t quirks)
{
    if (quirks & VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_4)
        return 4;
    else if (quirks & VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_8)
        return 8;
    else if (quirks & VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_12)
        return 12;
    else if (quirks & VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_16)
        return 16;
    else if (quirks & VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_32)
        return 32;

    return 0;
}

#define VKD3D_DXBC_MAX_SOURCE_COUNT 6
#define VKD3D_DXBC_HEADER_SIZE (8 * sizeof(uint32_t))

/* DXIL support */
int vkd3d_shader_compile_dxil(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv,
        struct vkd3d_shader_code_debug *spirv_debug,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compiler_args,
        bool is_dxil);

#endif  /* __VKD3D_SHADER_PRIVATE_H */
