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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_COUNT
#include "vkd3d_debug.h"
#include "vkd3d_threads.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define VKD3D_DEBUG_BUFFER_COUNT 64
#define VKD3D_DEBUG_BUFFER_SIZE 512

static const char *debug_level_names[] =
{
    /* VKD3D_DBG_LEVEL_UNKNOWN */ NULL,
    /* VKD3D_DBG_LEVEL_NONE    */ "none",
    /* VKD3D_DBG_LEVEL_ERR     */ "err",
    /* VKD3D_DBG_LEVEL_FIXME   */ "fixme",
    /* VKD3D_DBG_LEVEL_WARN    */ "warn",
    /* VKD3D_DBG_LEVEL_TRACE   */ "trace",
};

static const char *env_for_channel[] =
{
    /* VKD3D_DBG_CHANNEL_API    */ "VKD3D_DEBUG",
    /* VKD3D_DBG_CHANNEL_SHADER */ "VKD3D_SHADER_DEBUG",
};

static unsigned int vkd3d_dbg_level[VKD3D_DBG_CHANNEL_COUNT];
static spinlock_t vkd3d_dbg_initialized;
static pthread_once_t vkd3d_dbg_once = PTHREAD_ONCE_INIT;

static void vkd3d_dbg_init_once(void)
{
    const char *vkd3d_debug;
    unsigned int channel, i;

    for (channel = 0; channel < VKD3D_DBG_CHANNEL_COUNT; channel++)
    {
        if (!(vkd3d_debug = getenv(env_for_channel[channel])))
            vkd3d_debug = "";

        for (i = 1; i < ARRAY_SIZE(debug_level_names); ++i)
            if (!strcmp(debug_level_names[i], vkd3d_debug))
                vkd3d_dbg_level[channel] = i;

        /* Default debug level. */
        if (vkd3d_dbg_level[channel] == VKD3D_DBG_LEVEL_UNKNOWN)
            vkd3d_dbg_level[channel] = VKD3D_DBG_LEVEL_FIXME;
    }

    vkd3d_atomic_uint32_store_explicit(&vkd3d_dbg_initialized, 1, vkd3d_memory_order_release);
}

static inline void vkd3d_dbg_init(void)
{
    /* Early out since we're going to be spamming calls to vkd3d_dbg_init() for every trace call. */
    if (!vkd3d_atomic_uint32_load_explicit(&vkd3d_dbg_initialized, vkd3d_memory_order_acquire))
        pthread_once(&vkd3d_dbg_once, vkd3d_dbg_init_once);
}

enum vkd3d_dbg_level vkd3d_dbg_get_level(enum vkd3d_dbg_channel channel)
{
    vkd3d_dbg_init();
    if (channel >= VKD3D_DBG_CHANNEL_COUNT)
        return VKD3D_DBG_LEVEL_FIXME;
    assert(vkd3d_dbg_level[channel] != VKD3D_DBG_LEVEL_UNKNOWN);
    return vkd3d_dbg_level[channel];
}

void vkd3d_dbg_printf(enum vkd3d_dbg_channel channel, enum vkd3d_dbg_level level, const char *function, const char *fmt, ...)
{
    va_list args;

    if (vkd3d_dbg_get_level(channel) < level)
        return;

    assert(level < ARRAY_SIZE(debug_level_names));

    fprintf(stderr, "%s:%s: ", debug_level_names[level], function);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}

static char *get_buffer(void)
{
    static char buffers[VKD3D_DEBUG_BUFFER_COUNT][VKD3D_DEBUG_BUFFER_SIZE];
    static LONG buffer_index;
    LONG current_index;

    current_index = InterlockedIncrement(&buffer_index) % ARRAY_SIZE(buffers);
    return buffers[current_index];
}

const char *vkd3d_dbg_vsprintf(const char *fmt, va_list args)
{
    char *buffer;

    buffer = get_buffer();
    vsnprintf(buffer, VKD3D_DEBUG_BUFFER_SIZE, fmt, args);
    buffer[VKD3D_DEBUG_BUFFER_SIZE - 1] = '\0';
    return buffer;
}

const char *vkd3d_dbg_sprintf(const char *fmt, ...)
{
    const char *buffer;
    va_list args;

    va_start(args, fmt);
    buffer = vkd3d_dbg_vsprintf(fmt, args);
    va_end(args);
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

static const char *debugstr_w16(const uint16_t *wstr)
{
    char *buffer, *ptr;
    uint16_t c;

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

static const char *debugstr_w32(const uint32_t *wstr)
{
    char *buffer, *ptr;
    uint32_t c;

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

const char *debugstr_w(const WCHAR *wstr, size_t wchar_size)
{
    if (wchar_size == 2)
        return debugstr_w16((const uint16_t *)wstr);
    return debugstr_w32((const uint32_t *)wstr);
}

unsigned int vkd3d_env_var_as_uint(const char *name, unsigned int default_value)
{
    const char *value = getenv(name);
    unsigned long r;
    char *end_ptr;

    if (value)
    {
        errno = 0;
        r = strtoul(value, &end_ptr, 0);
        if (!errno && end_ptr != value)
            return min(r, UINT_MAX);
    }

    return default_value;
}

static bool is_option_separator(char c)
{
    return c == ',' || c == ';' || c == '\0';
}

bool vkd3d_debug_list_has_member(const char *string, const char *member)
{
    char prev_char, next_char;
    const char *p;

    p = string;
    while (p)
    {
        if ((p = strstr(p, member)))
        {
            prev_char = p > string ? p[-1] : 0;
            p += strlen(member);
            next_char = *p;

            if (is_option_separator(prev_char) && is_option_separator(next_char))
                return true;
        }
    }

    return false;
}

uint64_t vkd3d_parse_debug_options(const char *string,
        const struct vkd3d_debug_option *options, unsigned int option_count)
{
    uint64_t flags = 0;
    unsigned int i;

    for (i = 0; i < option_count; ++i)
    {
        const struct vkd3d_debug_option *opt = &options[i];

        if (vkd3d_debug_list_has_member(string, opt->name))
            flags |= opt->flag;
    }

    return flags;
}
