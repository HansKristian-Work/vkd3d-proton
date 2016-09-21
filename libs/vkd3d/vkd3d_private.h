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

#ifndef __VKD3D_PRIVATE_H
#define __VKD3D_PRIVATE_H

#define COBJMACROS
#include "vkd3d_common.h"
#include "vkd3d_debug.h"

#include "d3d12.h"

#include <assert.h>

struct d3d12_device;

/* ID3D12CommandAllocator */
struct d3d12_command_allocator
{
    ID3D12CommandAllocator ID3D12CommandAllocator_iface;
    ULONG refcount;

    D3D12_COMMAND_LIST_TYPE type;

    struct d3d12_device *device;
};

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator **allocator) DECLSPEC_HIDDEN;

/* ID3D12CommandQueue */
struct d3d12_command_queue
{
    ID3D12CommandQueue ID3D12CommandQueue_iface;
    ULONG refcount;

    D3D12_COMMAND_QUEUE_DESC desc;

    struct d3d12_device *device;
};

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, struct d3d12_command_queue **queue) DECLSPEC_HIDDEN;

/* ID3D12Device */
struct d3d12_device
{
    ID3D12Device ID3D12Device_iface;
    ULONG refcount;
};

HRESULT d3d12_device_create(struct d3d12_device **device) DECLSPEC_HIDDEN;

/* utils */
BOOL is_valid_feature_level(D3D_FEATURE_LEVEL feature_level) DECLSPEC_HIDDEN;
BOOL check_feature_level_support(D3D_FEATURE_LEVEL feature_level) DECLSPEC_HIDDEN;

HRESULT return_interface(IUnknown *iface, REFIID iface_riid,
        REFIID requested_riid, void **object) DECLSPEC_HIDDEN;

static inline void *vkd3d_malloc(size_t size)
{
    void *ptr;
    if (!(ptr = malloc(size)))
        ERR("Out of memory.\n");
    return ptr;
}

static inline void *vkd3d_calloc(size_t count, size_t size)
{
    void *ptr;
    if (!(ptr = calloc(count, size)))
        ERR("Out of memory.\n");
    return ptr;
}

static inline void vkd3d_free(void *ptr)
{
    free(ptr);
}

#endif  /* __VKD3D_PRIVATE_H */
