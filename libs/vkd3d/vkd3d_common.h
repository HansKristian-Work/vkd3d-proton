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

#ifndef __VKD3D_COMMON_H
#define __VKD3D_COMMON_H

#include "config.h"
#include "vkd3d_windows.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

#if HAVE_SYNC_ADD_AND_FETCH
static inline ULONG InterlockedIncrement(ULONG volatile *x)
{
    return __sync_add_and_fetch(x, 1);
}
#else
# error "InterlockedIncrement not implemented for this platform"
#endif  /* HAVE_SYNC_ADD_AND_FETCH */

#if HAVE_SYNC_SUB_AND_FETCH
static inline ULONG InterlockedDecrement(ULONG volatile *x)
{
    return __sync_sub_and_fetch(x, 1);
}
#else
# error "InterlockedDecrement not implemented for this platform"
#endif

#endif  /* __VKD3D_COMMON_H */
