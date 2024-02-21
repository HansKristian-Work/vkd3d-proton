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

#include "vkd3d_platform.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define VKD3D_DEBUG_BUFFER_COUNT 64
#define VKD3D_DEBUG_BUFFER_SIZE 512

static const char *debug_level_names[] =
{
    /* VKD3D_DBG_LEVEL_UNKNOWN */ NULL,
    /* VKD3D_DBG_LEVEL_NONE    */ "none",
    /* VKD3D_DBG_LEVEL_ERR     */ "err",
    /* VKD3D_DBG_LEVEL_INFO    */ "info",
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
static FILE *vkd3d_log_file;
static bool vkd3d_disable_file;

void vkd3d_dbg_disable_debug_file(void)
{
    vkd3d_disable_file = true;
}

#ifdef _WIN32
typedef int (*PFN_wine_log)(const char *);
static PFN_wine_log wine_log_output;
#endif

/* With breadcrumbs trace and similar intensive logging operations,
 * reduce stdio/syscall overhead to an absolute minimum. */
struct vkd3d_string_stream
{
    char *buffer;
    size_t offset;
    size_t size;
};
static struct vkd3d_string_stream vkd3d_dbg_buffer;

static void vkd3d_dbg_init_once(void)
{
    char vkd3d_debug[VKD3D_PATH_MAX];
    unsigned int channel, i;

    for (channel = 0; channel < VKD3D_DBG_CHANNEL_COUNT; channel++)
    {
        if (!vkd3d_get_env_var(env_for_channel[channel], vkd3d_debug, sizeof(vkd3d_debug)))
            strncpy(vkd3d_debug, "", VKD3D_PATH_MAX);

        for (i = 1; i < ARRAY_SIZE(debug_level_names); ++i)
            if (!strcmp(debug_level_names[i], vkd3d_debug))
                vkd3d_dbg_level[channel] = i;

        /* Default debug level. */
        if (vkd3d_dbg_level[channel] == VKD3D_DBG_LEVEL_UNKNOWN)
            vkd3d_dbg_level[channel] = VKD3D_DBG_LEVEL_FIXME;
    }

    if (vkd3d_get_env_var("VKD3D_LOG_BUFFERED", vkd3d_debug, sizeof(vkd3d_debug)))
    {
        vkd3d_dbg_buffer.offset = 0;
        vkd3d_dbg_buffer.size = strtoul(vkd3d_debug, NULL, 0);
        if (!vkd3d_dbg_buffer.size)
            vkd3d_dbg_buffer.size = 64 * 1024;
        fprintf(stderr, "Using VKD3D_LOG_BUFFERED with %zu byte chunks.\n", vkd3d_dbg_buffer.size);
        vkd3d_dbg_buffer.buffer = malloc(vkd3d_dbg_buffer.size);
    }

    if (!vkd3d_disable_file && vkd3d_get_env_var("VKD3D_LOG_FILE", vkd3d_debug, sizeof(vkd3d_debug)))
    {
        /* Avoid extra formatting overhead when using buffered. */
        vkd3d_log_file = fopen(vkd3d_debug, vkd3d_dbg_buffer.buffer ? "wb" : "w");
        if (!vkd3d_log_file)
        {
            fprintf(stderr, "Failed to open log file: %s!\n", vkd3d_debug);
            fflush(stderr);
        }
    }
    else
    {
#ifdef _WIN32
        HMODULE module = LoadLibraryA("ntdll.dll");
        if (module)
            wine_log_output = (void*)GetProcAddress(module, "__wine_dbg_output");
#endif
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

static spinlock_t vkd3d_debug_buffer_spin;

void vkd3d_dbg_flush(void)
{
    if (vkd3d_dbg_buffer.buffer)
    {
        spinlock_acquire(&vkd3d_debug_buffer_spin);
        if (vkd3d_dbg_buffer.offset)
        {
            if (vkd3d_log_file)
            {
                fwrite(vkd3d_dbg_buffer.buffer, 1, vkd3d_dbg_buffer.offset, vkd3d_log_file);
            }
            else
            {
                /* Binary vs text matters on Win32.
                 * Don't bother trying to be clever here reopening stdio files as O_BINARY, etc. */
                fputs(vkd3d_dbg_buffer.buffer, stderr);
            }

            vkd3d_dbg_buffer.offset = 0;
            fflush(vkd3d_log_file ? vkd3d_log_file : stderr);
        }
        spinlock_release(&vkd3d_debug_buffer_spin);
    }
}

void vkd3d_dbg_printf(enum vkd3d_dbg_channel channel, enum vkd3d_dbg_level level, const char *function, const char *fmt, ...)
{
    unsigned int tid;
    FILE *log_file;
    va_list args;

    if (vkd3d_dbg_get_level(channel) < level)
        return;
    assert(level < ARRAY_SIZE(debug_level_names));

    log_file = vkd3d_log_file ? vkd3d_log_file : stderr;

    va_start(args, fmt);
    tid = vkd3d_get_current_thread_id();

    if (vkd3d_dbg_buffer.buffer)
    {
        char prefix_buffer[256];
        int prefix_buffer_count;
        char local_buffer[4096];
        int local_buffer_count;
        int required_count;

        prefix_buffer_count = snprintf(prefix_buffer, sizeof(prefix_buffer),
                "%04x:%s:%s: ", tid, debug_level_names[level], function);
        local_buffer_count = vsnprintf(local_buffer, sizeof(local_buffer), fmt, args);
        required_count = prefix_buffer_count + local_buffer_count;

        spinlock_acquire(&vkd3d_debug_buffer_spin);
        if (vkd3d_dbg_buffer.offset + required_count > vkd3d_dbg_buffer.size)
        {
            if (vkd3d_log_file)
            {
                fwrite(vkd3d_dbg_buffer.buffer, 1, vkd3d_dbg_buffer.offset, vkd3d_log_file);
            }
            else
            {
                /* Binary vs text matters on Win32.
                 * Don't bother trying to be clever here reopening stdio files as O_BINARY, etc. */
                fputs(vkd3d_dbg_buffer.buffer, stderr);
            }

            vkd3d_dbg_buffer.offset = 0;
        }

        /* Here we trade performance for robustness. Some data will be left behind on early termination or crash. */
        if (vkd3d_dbg_buffer.offset + required_count <= vkd3d_dbg_buffer.size)
        {
            memcpy(vkd3d_dbg_buffer.buffer + vkd3d_dbg_buffer.offset, prefix_buffer, prefix_buffer_count);
            vkd3d_dbg_buffer.offset += prefix_buffer_count;
            memcpy(vkd3d_dbg_buffer.buffer + vkd3d_dbg_buffer.offset, local_buffer, local_buffer_count);
            vkd3d_dbg_buffer.offset += local_buffer_count;
        }
        else
        {
            /* If we cannot buffer up, just emit inline. */
            fputs(prefix_buffer, log_file);
            fputs(local_buffer, log_file);
        }
        spinlock_release(&vkd3d_debug_buffer_spin);
    }
#ifdef _WIN32
    else if (wine_log_output)
    {
        char local_buffer[4096];
        uint64_t ticks;
        size_t offset;

        /* Try to match format of Wine log output. */
        ticks = vkd3d_get_current_time_ns();
        offset = snprintf(local_buffer, sizeof(local_buffer),
                "%3u.%03u:%04x:%04x:%s:vkd3d-proton:%s: ",
                (unsigned int)(ticks / 1000000000), (unsigned int)((ticks % 1000000000) / 1000000),
                (UINT)GetCurrentProcessId(), tid,
                debug_level_names[level], function);
        if (offset < sizeof(local_buffer))
            vsnprintf(local_buffer + offset, sizeof(local_buffer) - offset, fmt, args);
        wine_log_output(local_buffer);
    }
#endif
    else
    {
        spinlock_acquire(&vkd3d_debug_buffer_spin);
        fprintf(log_file, "%04x:%s:%s: ", tid, debug_level_names[level], function);
        vfprintf(log_file, fmt, args);
        spinlock_release(&vkd3d_debug_buffer_spin);
        fflush(log_file);
    }
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

unsigned int vkd3d_env_var_as_uint(const char *name, unsigned int default_value)
{
    char value[VKD3D_PATH_MAX];
    unsigned long r;
    char *end_ptr;

    if (vkd3d_get_env_var(name, value, sizeof(value)) && strlen(value) > 0)
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
