/*
 * Copyright 2021 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_STRING_H
#define __VKD3D_STRING_H

#include "vkd3d_common.h"
#include "hashmap.h"
#include <stddef.h>

/* Various string utilities. */

WCHAR *vkd3d_dup_entry_point(const char *str);
WCHAR *vkd3d_dup_entry_point_n(const char *str, size_t len);

bool vkd3d_export_strequal(const WCHAR *a, const WCHAR *b);
bool vkd3d_export_strequal_mixed(const WCHAR *a, const char *b);
bool vkd3d_export_strequal_substr(const WCHAR *a, size_t n, const WCHAR *b);

char *vkd3d_strdup(const char *str);
char *vkd3d_strdup_n(const char *str, size_t n);
WCHAR *vkd3d_wstrdup(const WCHAR *str);
WCHAR *vkd3d_wstrdup_n(const WCHAR *str, size_t n);

static inline bool vkd3d_string_ends_with_n(const char *str, size_t str_len, const char *ending, size_t ending_len)
{
    return str_len >= ending_len && !strncmp(str + (str_len - ending_len), ending, ending_len);
}

static inline bool vkd3d_string_ends_with(const char *str, const char *ending)
{
    return vkd3d_string_ends_with_n(str, strlen(str), ending, strlen(ending));
}

enum vkd3d_string_compare_mode
{
    VKD3D_STRING_COMPARE_NEVER,
    VKD3D_STRING_COMPARE_ALWAYS,
    VKD3D_STRING_COMPARE_EXACT,
    VKD3D_STRING_COMPARE_STARTS_WITH,
    VKD3D_STRING_COMPARE_ENDS_WITH,
    VKD3D_STRING_COMPARE_CONTAINS,
    VKD3D_STRING_COMPARE_HASH_EQUAL,
};

static inline bool vkd3d_string_compare(enum vkd3d_string_compare_mode mode, const char *string, const char *comparator)
{
    uint64_t str_hash, comp_hash;

    switch (mode)
    {
        default:
        case VKD3D_STRING_COMPARE_NEVER:
            return false;
        case VKD3D_STRING_COMPARE_ALWAYS:
            return true;
        case VKD3D_STRING_COMPARE_EXACT:
            return !strcmp(string, comparator);
        case VKD3D_STRING_COMPARE_STARTS_WITH:
            return !strncmp(string, comparator, strlen(comparator));
        case VKD3D_STRING_COMPARE_ENDS_WITH:
            return vkd3d_string_ends_with(string, comparator);
        case VKD3D_STRING_COMPARE_CONTAINS:
            return strstr(string, comparator) != NULL;
        case VKD3D_STRING_COMPARE_HASH_EQUAL:
            comp_hash = strtoull(comparator, NULL, 16);
            str_hash = hash_fnv1_iterate_string(hash_fnv1_init(), string);
            return comp_hash == str_hash;
    }
}

static inline void vkd3d_strlcpy(char *dst, size_t dst_size, const char *src)
{
    if (dst_size > 0)
    {
        if (strlen(src) < dst_size)
        {
            strcpy(dst, src);
        }
        else
        {
            strncpy(dst, src, dst_size - 1);
            dst[dst_size - 1] = '\0';
        }
    }
}

static inline void vkd3d_strlcat(char *dst, size_t dst_size, const char *src)
{
    if (dst_size > 0)
    {
        strncat(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

#endif /* __VKD3D_STRING_H */