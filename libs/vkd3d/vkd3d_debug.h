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

#ifndef __VKD3D_DEBUG_H
#define __VKD3D_DEBUG_H

#include "vkd3d_common.h"

#ifdef __GNUC__
# define VKD3D_PRINTF_FUNC(fmt, args) __attribute__((format(printf, fmt, args)))
#else
# define VKD3D_PRINTF_FUNC(fmt, args)
#endif  /* __GNUC__ */

enum vkd3d_dbg_level
{
    VKD3D_DBG_LEVEL_NONE,
    VKD3D_DBG_LEVEL_ERR,
    VKD3D_DBG_LEVEL_FIXME,
    VKD3D_DBG_LEVEL_WARN,
    VKD3D_DBG_LEVEL_TRACE,
};

void vkd3d_dbg_printf(enum vkd3d_dbg_level level, const char *function,
        const char *fmt, ...) VKD3D_PRINTF_FUNC(3, 4) DECLSPEC_HIDDEN;

const char *vkd3d_dbg_sprintf(const char *fmt, ...) VKD3D_PRINTF_FUNC(1, 2) DECLSPEC_HIDDEN;
const char *debugstr_w(const WCHAR *wstr) DECLSPEC_HIDDEN;

#define VKD3D_DBG_LOG(level) \
        do { \
        const enum vkd3d_dbg_level __level = VKD3D_DBG_LEVEL_##level; \
        VKD3D_DBG_PRINTF

#define VKD3D_DBG_PRINTF(args...) \
        vkd3d_dbg_printf(__level, __FUNCTION__, args); } while (0)

#define TRACE VKD3D_DBG_LOG(TRACE)
#define WARN  VKD3D_DBG_LOG(WARN)
#define FIXME VKD3D_DBG_LOG(FIXME)
#define ERR   VKD3D_DBG_LOG(ERR)

static inline const char *debugstr_uint64(UINT64 v)
{
    if ((v >> 32) && sizeof(unsigned long) < sizeof(v))
        return vkd3d_dbg_sprintf("%#lx%08lx", (unsigned long)(v >> 32), (unsigned long)v);

    return vkd3d_dbg_sprintf("%#lx", (unsigned long)v);
}

static inline const char *debugstr_guid(const GUID *guid)
{
    if (!guid)
        return "(null)";

    return vkd3d_dbg_sprintf("{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            (unsigned long)guid->Data1, guid->Data2, guid->Data3, guid->Data4[0],
            guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4],
            guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

#endif  /* __VKD3D_DEBUG_H */
