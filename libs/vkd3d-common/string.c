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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_string.h"
#include "vkd3d_memory.h"

STATIC_ASSERT(sizeof(WCHAR) == sizeof(uint16_t));

char *vkd3d_strdup(const char *str)
{
    /* strdup() is actually not standard. */
    char *duped;
    size_t len;

    len = strlen(str) + 1;

    duped = vkd3d_malloc(len);
    if (duped)
        memcpy(duped, str, len);
    return duped;
}

WCHAR *vkd3d_wstrdup(const WCHAR *str)
{
    WCHAR *duped;
    size_t len;

    len = vkd3d_wcslen(str) + 1;

    duped = vkd3d_malloc(len * sizeof(WCHAR));
    if (duped)
        memcpy(duped, str, len * sizeof(WCHAR));
    return duped;
}

bool vkd3d_export_strequal(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b)
        return false;

    while (*a != '\0' && *b != '\0')
    {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool vkd3d_export_strequal_substr(const WCHAR *a, size_t expected_n, const WCHAR *b)
{
    size_t n = 0;

    if (!a || !b)
        return false;

    while (*a != '\0' && *b != '\0' && n < expected_n)
    {
        if (*a != *b)
            return false;
        a++;
        b++;
        n++;
    }

    return n == expected_n && *b == '\0';
}

WCHAR *vkd3d_dup_entry_point(const char *str)
{
    return vkd3d_dup_entry_point_n(str, strlen(str));
}

WCHAR *vkd3d_dup_entry_point_n(const char *str, size_t len)
{
    WCHAR *duped;
    size_t i;

    duped = vkd3d_malloc((len + 1) * sizeof(WCHAR));
    if (!duped)
        return NULL;

    for (i = 0; i < len; i++)
        duped[i] = (unsigned char)str[i];
    duped[len] = 0;
    return duped;
}

static bool is_valid_identifier_character(char v)
{
    return (v >= 'a' && v <= 'z') || (v >= 'A' && v <= 'Z') || v == '_';
}

WCHAR *vkd3d_dup_demangled_entry_point(const char *entry)
{
    const char *end_entry;

    while (*entry != '\0' && !is_valid_identifier_character(*entry))
        entry++;

    end_entry = entry;
    while (*end_entry != '\0' && is_valid_identifier_character(*end_entry))
        end_entry++;

    if (entry == end_entry)
        return NULL;

    return vkd3d_dup_entry_point_n(entry, end_entry - entry);
}
