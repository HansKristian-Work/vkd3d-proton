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

#include "vkd3d_private.h"

/* ID3D12CommandAllocator */
static inline struct d3d12_command_allocator *impl_from_ID3D12CommandAllocator(ID3D12CommandAllocator *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_allocator, ID3D12CommandAllocator_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_QueryInterface(ID3D12CommandAllocator *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12CommandAllocator)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12CommandAllocator_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_command_allocator_AddRef(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedIncrement(&allocator->refcount);

    TRACE("%p increasing refcount to %u.\n", allocator, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_allocator_Release(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedDecrement(&allocator->refcount);

    TRACE("%p decreasing refcount to %u.\n", allocator, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = allocator->device;

        vkd3d_free(allocator);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_GetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetPrivateDataInterface(ID3D12CommandAllocator *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetName(ID3D12CommandAllocator *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_GetDevice(ID3D12CommandAllocator *iface,
        REFIID riid, void **device)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&allocator->device->ID3D12Device_iface, riid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_Reset(ID3D12CommandAllocator *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static const struct ID3D12CommandAllocatorVtbl d3d12_command_allocator_vtbl =
{
    /* IUnknown methods */
    d3d12_command_allocator_QueryInterface,
    d3d12_command_allocator_AddRef,
    d3d12_command_allocator_Release,
    /* ID3D12Object methods */
    d3d12_command_allocator_GetPrivateData,
    d3d12_command_allocator_SetPrivateData,
    d3d12_command_allocator_SetPrivateDataInterface,
    d3d12_command_allocator_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_allocator_GetDevice,
    /* ID3D12CommandAllocator methods */
    d3d12_command_allocator_Reset,
};

static void d3d12_command_allocator_init(struct d3d12_command_allocator *allocator,
        struct d3d12_device *device, D3D12_COMMAND_LIST_TYPE type)
{
    allocator->ID3D12CommandAllocator_iface.lpVtbl = &d3d12_command_allocator_vtbl;
    allocator->refcount = 1;

    allocator->type = type;

    allocator->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);
}

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator **allocator)
{
    struct d3d12_command_allocator *object;

    if (!(D3D12_COMMAND_LIST_TYPE_DIRECT <= type && type <= D3D12_COMMAND_LIST_TYPE_COPY))
    {
        WARN("Invalid type %#x.\n", type);
        return E_INVALIDARG;
    }

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    d3d12_command_allocator_init(object, device, type);

    TRACE("Created command allocator %p.\n", object);

    *allocator = object;

    return S_OK;
}

/* ID3D12CommandQueue */
static inline struct d3d12_command_queue *impl_from_ID3D12CommandQueue(ID3D12CommandQueue *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_queue, ID3D12CommandQueue_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_QueryInterface(ID3D12CommandQueue *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12CommandQueue)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12CommandQueue_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_command_queue_AddRef(ID3D12CommandQueue *iface)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    ULONG refcount = InterlockedIncrement(&command_queue->refcount);

    TRACE("%p increasing refcount to %u.\n", command_queue, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_queue_Release(ID3D12CommandQueue *iface)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    ULONG refcount = InterlockedDecrement(&command_queue->refcount);

    TRACE("%p decreasing refcount to %u.\n", command_queue, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = command_queue->device;

        vkd3d_free(command_queue);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetPrivateData(ID3D12CommandQueue *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetPrivateData(ID3D12CommandQueue *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetPrivateDataInterface(ID3D12CommandQueue *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetName(ID3D12CommandQueue *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetDevice(ID3D12CommandQueue *iface,
        REFIID riid, void **device)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&command_queue->device->ID3D12Device_iface, riid, device);
}

static void STDMETHODCALLTYPE d3d12_command_queue_UpdateTileMappings(ID3D12CommandQueue *iface,
        ID3D12Resource *resource, UINT region_count,
        const D3D12_TILED_RESOURCE_COORDINATE *region_start_coordinates,
        const D3D12_TILE_REGION_SIZE *region_sizes,
        UINT range_count,
        const D3D12_TILE_RANGE_FLAGS *range_flags,
        UINT *heap_range_offsets,
        UINT *range_tile_counts,
        D3D12_TILE_MAPPING_FLAGS flags)
{
    FIXME("iface %p, resource %p, region_count %u, region_start_coordinates %p, "
            "region_sizes %p, range_count %u, range_flags %p, heap_range_offsets %p, "
            "range_tile_counts %p, flags %#x stub!\n",
            iface, resource, region_count, region_start_coordinates, region_sizes, range_count,
            range_flags, heap_range_offsets, range_tile_counts, flags);
}

static void STDMETHODCALLTYPE d3d12_command_queue_CopyTileMappings(ID3D12CommandQueue *iface,
        ID3D12Resource *dst_resource,
        const D3D12_TILED_RESOURCE_COORDINATE *dst_region_start_coordinate,
        ID3D12Resource *src_resource,
        const D3D12_TILED_RESOURCE_COORDINATE *src_region_start_coordinate,
        const D3D12_TILE_REGION_SIZE *region_size,
        D3D12_TILE_MAPPING_FLAGS flags)
{
    FIXME("iface %p, dst_resource %p, dst_region_start_coordinate %p, "
            "src_resource %p, src_region_start_coordinate %p, region_size %p, flags %#x stub!\n",
            iface, dst_resource, dst_region_start_coordinate, src_resource,
            src_region_start_coordinate, region_size, flags);
}

static void STDMETHODCALLTYPE d3d12_command_queue_ExecuteCommandLists(ID3D12CommandQueue *iface,
        UINT command_list_count, ID3D12CommandList * const *command_lists)
{
    FIXME("iface %p, command_list_count %u, command_lists %p stub!\n",
            iface, command_list_count, command_lists);
}

static void STDMETHODCALLTYPE d3d12_command_queue_SetMarker(ID3D12CommandQueue *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metadata %#x, datap %p, size %u stub!\n",
            iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_queue_BeginEvent(ID3D12CommandQueue *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metatdata %#x, data %p, size %u stub!\n",
            iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_queue_EndEvent(ID3D12CommandQueue *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_Signal(ID3D12CommandQueue *iface,
        ID3D12Fence *fence, UINT64 value)
{
    FIXME("iface %p, fence %p, value %s stub!\n", iface, fence, debugstr_uint64(value));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_Wait(ID3D12CommandQueue *iface,
        ID3D12Fence *fence, UINT64 value)
{
    FIXME("iface %p, fence %p, value %s stub!\n", iface, fence, debugstr_uint64(value));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetTimestampFrequency(ID3D12CommandQueue *iface,
        UINT64 *frequency)
{
    FIXME("iface %p, frequency %p stub!\n", iface, frequency);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetClockCalibration(ID3D12CommandQueue *iface,
        UINT64 *gpu_timestamp, UINT64 *cpu_timestamp)
{
    FIXME("iface %p, gpu_timestamp %p, cpu_timestamp %p stub!\n",
            iface, gpu_timestamp, cpu_timestamp);

    return E_NOTIMPL;
}

static D3D12_COMMAND_QUEUE_DESC * STDMETHODCALLTYPE d3d12_command_queue_GetDesc(ID3D12CommandQueue *iface,
        D3D12_COMMAND_QUEUE_DESC *desc)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = command_queue->desc;
    return desc;
}

static const struct ID3D12CommandQueueVtbl d3d12_command_queue_vtbl =
{
    /* IUnknown methods */
    d3d12_command_queue_QueryInterface,
    d3d12_command_queue_AddRef,
    d3d12_command_queue_Release,
    /* ID3D12Object methods */
    d3d12_command_queue_GetPrivateData,
    d3d12_command_queue_SetPrivateData,
    d3d12_command_queue_SetPrivateDataInterface,
    d3d12_command_queue_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_queue_GetDevice,
    /* ID3D12CommandQueue methods */
    d3d12_command_queue_UpdateTileMappings,
    d3d12_command_queue_CopyTileMappings,
    d3d12_command_queue_ExecuteCommandLists,
    d3d12_command_queue_SetMarker,
    d3d12_command_queue_BeginEvent,
    d3d12_command_queue_EndEvent,
    d3d12_command_queue_Signal,
    d3d12_command_queue_Wait,
    d3d12_command_queue_GetTimestampFrequency,
    d3d12_command_queue_GetClockCalibration,
    d3d12_command_queue_GetDesc,
};

static void d3d12_command_queue_init(struct d3d12_command_queue *queue,
        struct d3d12_device *device, const D3D12_COMMAND_QUEUE_DESC *desc)
{
    queue->ID3D12CommandQueue_iface.lpVtbl = &d3d12_command_queue_vtbl;
    queue->refcount = 1;

    queue->desc = *desc;
    if (!queue->desc.NodeMask)
        queue->desc.NodeMask = 0x1;

    queue->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);
}

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, struct d3d12_command_queue **queue)
{
    struct d3d12_command_queue *object;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    d3d12_command_queue_init(object, device, desc);

    TRACE("Created command queue %p.\n", object);

    *queue = object;

    return S_OK;
}
