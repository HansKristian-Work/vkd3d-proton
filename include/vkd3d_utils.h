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

#ifndef __VKD3D_UTILS_H
#define __VKD3D_UTILS_H

#include "vkd3d.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define WAIT_OBJECT_0 (0)
#define WAIT_TIMEOUT (1)
#define WAIT_FAILED (~0u)
#define INFINITE (~0u)

HANDLE WINAPI VKD3DCreateEvent(void);
BOOL WINAPI VKD3DSignalEvent(HANDLE event);
unsigned int WINAPI VKD3DWaitEvent(HANDLE event, unsigned int milliseconds);
void WINAPI VKD3DDestroyEvent(HANDLE event);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_UTILS_H */
