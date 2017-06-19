/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#include "vkd3d_debug.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VKD3D_DEBUG_BUFFER_COUNT 64
#define VKD3D_DEBUG_BUFFER_SIZE 512

static const char *debug_level_names[] =
{
    /* VKD3D_DBG_LEVEL_NONE  */ "none",
    /* VKD3D_DBG_LEVEL_ERR   */ "err",
    /* VKD3D_DBG_LEVEL_FIXME */ "fixme",
    /* VKD3D_DBG_LEVEL_WARN  */ "warn",
    /* VKD3D_DBG_LEVEL_TRACE */ "trace",
};

enum vkd3d_dbg_level vkd3d_dbg_get_level(void)
{
    static unsigned int level = ~0u;
    const char *vkd3d_debug;
    unsigned int i;

    if (level != ~0u)
        return level;

    if (!(vkd3d_debug = getenv("VKD3D_DEBUG")))
        vkd3d_debug = "";

    for (i = 0; i < ARRAY_SIZE(debug_level_names); ++i)
    {
        if (!strcmp(debug_level_names[i], vkd3d_debug))
        {
            level = i;
            return level;
        }
    }

    /* Default debug level. */
    level = VKD3D_DBG_LEVEL_FIXME;
    return level;
}

void vkd3d_dbg_printf(enum vkd3d_dbg_level level, const char *function, const char *fmt, ...)
{
    va_list args;

    if (vkd3d_dbg_get_level() < level)
        return;

    assert(level <= ARRAY_SIZE(debug_level_names));

    fprintf(stderr, "%s:%s: ", debug_level_names[level], function);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static char *get_buffer(void)
{
    static char buffers[VKD3D_DEBUG_BUFFER_COUNT][VKD3D_DEBUG_BUFFER_SIZE];
    static LONG buffer_index;
    LONG current_index;

    current_index = InterlockedIncrement(&buffer_index) % ARRAY_SIZE(buffers);
    return buffers[current_index];
}

const char *vkd3d_dbg_sprintf(const char *fmt, ...)
{
    char *buffer;
    va_list args;
    int size;

    buffer = get_buffer();
    va_start(args, fmt);
    size = vsnprintf(buffer, VKD3D_DEBUG_BUFFER_SIZE, fmt, args);
    va_end(args);
    if (size == -1 || size >= VKD3D_DEBUG_BUFFER_SIZE)
        buffer[VKD3D_DEBUG_BUFFER_SIZE - 1] = '\0';
    return buffer;
}

const char *debugstr_a(const char *str)
{
    char *buffer, *ptr;
    char c;

    if (!str)
        return "(null)";

    ptr = buffer = get_buffer();

    *ptr++ = '"';
    while ((c = *str++) && ptr <= buffer + VKD3D_DEBUG_BUFFER_SIZE - 8)
    {
        int escape_char;

        switch (c)
        {
            case '"':
            case '\\':
            case '\n':
            case '\r':
            case '\t':
                escape_char = c;
                break;
            default:
                escape_char = 0;
                break;
        }

        if (escape_char)
        {
            *ptr++ = '\\';
            *ptr++ = escape_char;
            continue;
        }

        if (isprint(c))
        {
            *ptr++ = c;
        }
        else
        {
            *ptr++ = '\\';
            sprintf(ptr, "%02x", c);
            ptr += 2;
        }
    }
    *ptr++ = '"';

    if (c)
    {
        *ptr++ = '.';
        *ptr++ = '.';
        *ptr++ = '.';
    }
    *ptr = '\0';

    return buffer;
}

const char *debugstr_w(const WCHAR *wstr)
{
    char *buffer, *ptr;
    WCHAR c;

    if (!wstr)
        return "(null)";

    ptr = buffer = get_buffer();

    *ptr++ = '"';
    while ((c = *wstr++) && ptr <= buffer + VKD3D_DEBUG_BUFFER_SIZE - 10)
    {
        int escape_char;

        switch (c)
        {
            case '"':
            case '\\':
            case '\n':
            case '\r':
            case '\t':
                escape_char = c;
                break;
            default:
                escape_char = 0;
                break;
        }

        if (escape_char)
        {
            *ptr++ = '\\';
            *ptr++ = escape_char;
            continue;
        }

        if (isprint(c))
        {
            *ptr++ = c;
        }
        else
        {
            *ptr++ = '\\';
            sprintf(ptr, "%04x", c);
            ptr += 4;
        }
    }
    *ptr++ = '"';

    if (c)
    {
        *ptr++ = '.';
        *ptr++ = '.';
        *ptr++ = '.';
    }
    *ptr = '\0';

    return buffer;
}
