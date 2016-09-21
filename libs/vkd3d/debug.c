/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "vkd3d_debug.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *debug_level_names[] =
{
    /* VKD3D_DBG_LEVEL_NONE  */ "none",
    /* VKD3D_DBG_LEVEL_ERR   */ "err",
    /* VKD3D_DBG_LEVEL_FIXME */ "fixme",
    /* VKD3D_DBG_LEVEL_WARN  */ "warn",
    /* VKD3D_DBG_LEVEL_TRACE */ "trace",
};

static enum vkd3d_dbg_level vkd3d_dbg_get_level(void)
{
    static unsigned int level = ~0u;
    const char *vkd3d_debug;
    unsigned int i;

    if (level != ~0u)
        return level;

    if (!(vkd3d_debug = getenv("VKD3DDEBUG")))
        vkd3d_debug = "";

    for (i = 0; i < sizeof(debug_level_names) / sizeof(*debug_level_names); ++i)
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

    assert(level <= sizeof(debug_level_names) / sizeof(*debug_level_names));

    printf("%s:%s: ", debug_level_names[level], function);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

#define VKD3D_DEBUG_BUFFER_SIZE 512

static char *get_buffer(void)
{
    static char buffers[64][VKD3D_DEBUG_BUFFER_SIZE];
    static unsigned int buffer_index;
    unsigned int current_index;

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
