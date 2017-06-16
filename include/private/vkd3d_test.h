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

#ifndef __VKD3D_TEST_H
#define __VKD3D_TEST_H

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vkd3d_common.h"

static void vkd3d_test_main(int argc, char **argv);
static const char *vkd3d_test_name;

#define START_TEST(name) \
        static const char *vkd3d_test_name = #name; \
        static void vkd3d_test_main(int argc, char **argv)

#define ok ok_(__LINE__)
#define todo todo_(__LINE__)
#define skip skip_(__LINE__)
#define trace trace_(__LINE__)

#define ok_(line) \
        do { \
        unsigned int vkd3d_line = line; \
        VKD3D_TEST_OK

#define VKD3D_TEST_OK(args...) \
        vkd3d_test_ok(vkd3d_line, args); } while (0)

#define todo_(line) \
        do { \
        unsigned int vkd3d_line = line; \
        VKD3D_TEST_TODO

#ifdef _WIN32
# define VKD3D_TEST_TODO(args...) \
        vkd3d_test_ok(vkd3d_line, args); } while (0)
#else
# define VKD3D_TEST_TODO(args...) \
        vkd3d_test_todo(vkd3d_line, args); } while (0)
#endif  /* _WIN32 */

#define skip_(line) \
        do { \
        unsigned int vkd3d_line = line; \
        VKD3D_TEST_SKIP

#define VKD3D_TEST_SKIP(args...) \
        vkd3d_test_skip(vkd3d_line, args); } while (0)

#define trace_(line) \
        do { \
        unsigned int vkd3d_line = line; \
        VKD3D_TEST_TRACE

#define VKD3D_TEST_TRACE(args...) \
        vkd3d_test_trace(vkd3d_line, args); } while (0)

static struct
{
    LONG success_count;
    LONG failure_count;
    LONG skip_count;
    LONG todo_count;
    LONG todo_success_count;

    unsigned int debug_level;
} vkd3d_test_state;

static void VKD3D_PRINTF_FUNC(3, 4) VKD3D_UNUSED
vkd3d_test_ok(unsigned int line, bool result, const char *fmt, ...)
{
    if (result)
    {
        if (vkd3d_test_state.debug_level > 1)
            printf("%s:%d: Test succeeded.\n", vkd3d_test_name, line);
        InterlockedIncrement(&vkd3d_test_state.success_count);
    }
    else
    {
        va_list args;
        va_start(args, fmt);
        printf("%s:%d: Test failed: ", vkd3d_test_name, line);
        vprintf(fmt, args);
        va_end(args);
        InterlockedIncrement(&vkd3d_test_state.failure_count);
    }
}

static void VKD3D_PRINTF_FUNC(3, 4) VKD3D_UNUSED
vkd3d_test_todo(unsigned int line, bool result, const char *fmt, ...)
{
    va_list args;

    if (result)
    {
        printf("%s:%d Todo succeeded: ", vkd3d_test_name, line);
        InterlockedIncrement(&vkd3d_test_state.todo_success_count);
    }
    else
    {
        printf("%s:%d: Todo: ", vkd3d_test_name, line);
        InterlockedIncrement(&vkd3d_test_state.todo_count);
    }

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static void VKD3D_PRINTF_FUNC(2, 3) VKD3D_UNUSED
vkd3d_test_skip(unsigned int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("%s:%d: Test skipped: ", vkd3d_test_name, line);
    vprintf(fmt, args);
    va_end(args);
    InterlockedIncrement(&vkd3d_test_state.skip_count);
}

static void VKD3D_PRINTF_FUNC(2, 3) VKD3D_UNUSED
vkd3d_test_trace(unsigned int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("%s:%d: ", vkd3d_test_name, line);
    vprintf(fmt, args);
    va_end(args);
}

static void VKD3D_PRINTF_FUNC(1, 2) VKD3D_UNUSED
vkd3d_test_debug(const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    int size;

    size = snprintf(buffer, sizeof(buffer), "%s: ", vkd3d_test_name);
    if (0 < size && size < sizeof(buffer))
    {
        va_start(args, fmt);
        vsnprintf(buffer + size, sizeof(buffer) - size, fmt, args);
        va_end(args);
    }
    buffer[sizeof(buffer) - 1] = '\0';

#ifdef _WIN32
    OutputDebugStringA(buffer);
#endif

    if (vkd3d_test_state.debug_level > 0)
        printf("%s\n", buffer);
}

int main(int argc, char **argv)
{
    const char *debug_level = getenv("VKD3D_TEST_DEBUG");

    memset(&vkd3d_test_state, 0, sizeof(vkd3d_test_state));
    vkd3d_test_state.debug_level = !debug_level ? 0 : atoi(debug_level);

    vkd3d_test_main(argc, argv);

    printf("%s: %lu tests executed (%lu failures, %lu skipped, %lu todo).\n",
            vkd3d_test_name,
            (unsigned long)(vkd3d_test_state.success_count
            + vkd3d_test_state.failure_count + vkd3d_test_state.todo_count
            + vkd3d_test_state.todo_success_count),
            (unsigned long)(vkd3d_test_state.failure_count
            + vkd3d_test_state.todo_success_count),
            (unsigned long)vkd3d_test_state.skip_count,
            (unsigned long)vkd3d_test_state.todo_count);

    return vkd3d_test_state.failure_count || vkd3d_test_state.todo_success_count;
}

#ifdef _WIN32
static char *vkd3d_test_strdupWtoA(WCHAR *str)
{
    char *out;
    int len;

    if (!(len = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL)))
        return NULL;
    if (!(out = malloc(len)))
        return NULL;
    WideCharToMultiByte(CP_ACP, 0, str, -1, out, len, NULL, NULL);

    return out;
}

int wmain(int argc, WCHAR **wargv)
{
    char **argv;
    int i, ret;

    argv = malloc(argc * sizeof(*argv));
    assert(argv);
    for (i = 0; i < argc; ++i)
    {
        if (!(argv[i] = vkd3d_test_strdupWtoA(wargv[i])))
            break;
    }
    assert(i == argc);

    ret = main(argc, argv);

    for (i = 0; i < argc; ++i)
        free(argv[i]);
    free(argv);

    return ret;
}
#endif  /* _WIN32 */

typedef void (*vkd3d_test_pfn)(void);

static inline void vkd3d_run_test(const char *name, vkd3d_test_pfn test_pfn)
{
    vkd3d_test_debug(name);
    test_pfn();
}

#define run_test(test_pfn) \
        vkd3d_run_test(#test_pfn, test_pfn)

#endif  /* __VKD3D_TEST_H */
