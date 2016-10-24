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

#ifndef __VKD3D_MEMORY_H
#define __VKD3D_MEMORY_H

#include <assert.h>
#include "vkd3d_debug.h"

static inline void *vkd3d_malloc(size_t size)
{
    void *ptr;
    if (!(ptr = malloc(size)))
        ERR("Out of memory.\n");
    return ptr;
}

static inline void *vkd3d_realloc(void *ptr, size_t size)
{
    if (!(ptr = realloc(ptr, size)))
        ERR("Out of memory.\n");
    return ptr;
}

static inline void *vkd3d_calloc(size_t count, size_t size)
{
    void *ptr;
    assert(count <= ~(size_t)0 / size);
    if (!(ptr = calloc(count, size)))
        ERR("Out of memory.\n");
    return ptr;
}

static inline void vkd3d_free(void *ptr)
{
    free(ptr);
}

#endif  /* __VKD3D_MEMORY_H */
