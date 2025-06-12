/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2019 Conor McCarthy for CodeWeavers
 * Copyright 2021 Philip Rebohle for Valve Corporation
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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_private.h"

/* ID3D12Heap */
static HRESULT STDMETHODCALLTYPE d3d12_heap_QueryInterface(d3d12_heap_iface *iface,
        REFIID iid, void **object)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);

    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(iid, &IID_ID3D12Heap)
            || IsEqualGUID(iid, &IID_ID3D12Heap1)
            || IsEqualGUID(iid, &IID_ID3D12Pageable)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12Heap1_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&heap->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &heap->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_heap_AddRef(d3d12_heap_iface *iface)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);
    ULONG refcount = InterlockedIncrement(&heap->refcount);

    if (refcount == 1)
    {
        d3d12_device_add_ref(heap->device);
        d3d12_heap_incref(heap);
    }

    TRACE("%p increasing refcount to %u.\n", heap, refcount);
    return refcount;
}

static void d3d12_heap_destroy(struct d3d12_heap *heap)
{
    TRACE("Destroying heap %p.\n", heap);

    d3d_destruction_notifier_free(&heap->destruction_notifier);

    vkd3d_free_memory(heap->device, &heap->device->memory_allocator, &heap->allocation);
    vkd3d_private_store_destroy(&heap->private_store);
    vkd3d_free(heap);
}

static void d3d12_heap_set_name(struct d3d12_heap *heap, const char *name)
{
    if (!heap->allocation.chunk)
        vkd3d_set_vk_object_name(heap->device, (uint64_t)heap->allocation.device_allocation.vk_memory,
                VK_OBJECT_TYPE_DEVICE_MEMORY, name);
}

static ULONG STDMETHODCALLTYPE d3d12_heap_Release(d3d12_heap_iface *iface)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);
    ULONG refcount = InterlockedDecrement(&heap->refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = heap->device;

        d3d_destruction_notifier_notify(&heap->destruction_notifier);

        d3d12_heap_decref(heap);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_GetPrivateData(d3d12_heap_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetPrivateData(d3d12_heap_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&heap->private_store, guid, data_size, data,
            (vkd3d_set_name_callback) d3d12_heap_set_name, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetPrivateDataInterface(d3d12_heap_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&heap->private_store, guid, data,
            (vkd3d_set_name_callback) d3d12_heap_set_name, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_GetDevice(d3d12_heap_iface *iface, REFIID iid, void **device)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(heap->device, iid, device);
}

static D3D12_HEAP_DESC * STDMETHODCALLTYPE d3d12_heap_GetDesc(d3d12_heap_iface *iface,
        D3D12_HEAP_DESC *desc)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap1(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = heap->desc;
    return desc;
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_GetProtectedResourceSession(d3d12_heap_iface *iface,
        REFIID iid, void **protected_session)
{
    FIXME("iface %p, iid %s, protected_session %p stub!", iface, debugstr_guid(iid), protected_session);

    return E_NOTIMPL;
}

ULONG d3d12_heap_incref(struct d3d12_heap *heap)
{
    ULONG refcount = InterlockedIncrement(&heap->internal_refcount);

    TRACE("%p increasing refcount to %u.\n", heap, refcount);

    return refcount;
}

ULONG d3d12_heap_decref(struct d3d12_heap *heap)
{
    ULONG refcount = InterlockedDecrement(&heap->internal_refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
        d3d12_heap_destroy(heap);

    return refcount;
}

CONST_VTBL struct ID3D12Heap1Vtbl d3d12_heap_vtbl =
{
    /* IUnknown methods */
    d3d12_heap_QueryInterface,
    d3d12_heap_AddRef,
    d3d12_heap_Release,
    /* ID3D12Object methods */
    d3d12_heap_GetPrivateData,
    d3d12_heap_SetPrivateData,
    d3d12_heap_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_heap_GetDevice,
    /* ID3D12Heap methods */
    d3d12_heap_GetDesc,
    /* ID3D12Heap1 methods */
    d3d12_heap_GetProtectedResourceSession,
};

HRESULT d3d12_device_validate_custom_heap_type(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties)
{
    if (heap_properties->Type != D3D12_HEAP_TYPE_CUSTOM)
        return S_OK;

    if (heap_properties->MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN)
    {
        WARN("Invalid memory pool preference.\n");
        return E_INVALIDARG;
    }

    if (heap_properties->MemoryPoolPreference == D3D12_MEMORY_POOL_L1
        && heap_properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK)
    {
        WARN("Invalid memory pool preference and CPU page property combination.\n");
        return E_INVALIDARG;
    }

    if (heap_properties->MemoryPoolPreference == D3D12_MEMORY_POOL_L1
        && d3d12_device_is_uma(device, NULL))
    {
        WARN("Invalid memory pool preference on UMA device.\n");
        return E_INVALIDARG;
    }

    if (heap_properties->MemoryPoolPreference == D3D12_MEMORY_POOL_L1
        && heap_properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE
        && !device->memory_info.has_gpu_upload_heap)
    {
        WARN("Invalid memory pool preference (device does not support rebar).\n");
        return E_INVALIDARG;
    }

    if (heap_properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN)
    {
        WARN("Must have explicit CPU page property for CUSTOM heap type.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT validate_heap_desc(struct d3d12_device *device, const D3D12_HEAP_DESC *desc)
{
    HRESULT hr;

    if (!desc->SizeInBytes)
    {
        WARN("Invalid size %"PRIu64".\n", desc->SizeInBytes);
        return E_INVALIDARG;
    }

    if (desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
            && desc->Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT)
    {
        WARN("Invalid alignment %"PRIu64".\n", desc->Alignment);
        return E_INVALIDARG;
    }

    if (desc->Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY)
    {
        WARN("D3D12_HEAP_FLAG_ALLOW_DISPLAY is only for committed resources.\n");
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_device_validate_custom_heap_type(device, &desc->Properties)))
        return hr;

    return S_OK;
}

static HRESULT d3d12_heap_init(struct d3d12_heap *heap, struct d3d12_device *device,
        const D3D12_HEAP_DESC *desc, void* host_address)
{
    struct vkd3d_allocate_heap_memory_info alloc_info;
    HRESULT hr;

    memset(heap, 0, sizeof(*heap));
    heap->ID3D12Heap_iface.lpVtbl = &d3d12_heap_vtbl;
    heap->internal_refcount = 1;
    heap->refcount = 1;
    heap->desc = *desc;
    heap->device = device;
    heap->priority.allows_dynamic_residency = false;
    spinlock_init(&heap->priority.spinlock);
    heap->priority.d3d12priority = D3D12_RESIDENCY_PRIORITY_NORMAL;
    heap->priority.residency_count = 1;

    if (!heap->desc.Properties.CreationNodeMask)
        heap->desc.Properties.CreationNodeMask = 1;
    if (!heap->desc.Properties.VisibleNodeMask)
        heap->desc.Properties.VisibleNodeMask = 1;
    if (!heap->desc.Alignment)
        heap->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    if (FAILED(hr = validate_heap_desc(device, &heap->desc)))
        return hr;

    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.heap_desc = heap->desc;
    alloc_info.host_ptr = host_address;

    if ((alloc_info.heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS) &&
            device->d3d12_caps.options.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2)
    {
        alloc_info.extra_allocation_flags = VKD3D_ALLOCATION_FLAG_ALLOW_IMAGE_SUBALLOCATION;
    }

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_DAMAGE_NOT_ZEROED_ALLOCATIONS))
    {
        /* Unfortunately, we cannot trust CREATE_NOT_ZEROED to actually do anything.
         * Stress tests on Windows suggest that it drivers always clear anyway.
         * This suggests we have a lot of potential game bugs in the wild that will randomly be exposed
         * if we try to skip clears.
         * For render targets, we expect the transition away from UNDEFINED to deal with it. */
        alloc_info.heap_desc.Flags &= ~D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
    }

    /* Buffers are far more sensitive to memory clears than images. */
    if ((alloc_info.heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS) &&
            (vkd3d_config_flags & VKD3D_CONFIG_FLAG_MEMORY_ALLOCATOR_SKIP_IMAGE_HEAP_CLEAR))
        alloc_info.heap_desc.Flags |= D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

    if (FAILED(hr = vkd3d_private_store_init(&heap->private_store)))
        return hr;

    if (device->device_info.memory_priority_features.memoryPriority)
    {
        /* this clause isn't trying to reproduce some precise d3d12 behavior,
           though it's hinted in the public docs that a similar prioritization
           is done there... and it seems like a good idea anyway. :) */
        if (heap->desc.Flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES)
        {
            uint32_t adjust = vkd3d_get_priority_adjust(heap->desc.SizeInBytes);
            heap->priority.d3d12priority = D3D12_RESIDENCY_PRIORITY_HIGH | adjust;
        }

        if (device->device_info.pageable_device_memory_features.pageableDeviceLocalMemory)
        {
            if (heap->desc.Flags & D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT)
                heap->priority.residency_count = 0;
        }
    }

    alloc_info.vk_memory_priority = heap->priority.residency_count ?
        vkd3d_convert_to_vk_prio(heap->priority.d3d12priority) : 0.f;

    if (FAILED(hr = vkd3d_allocate_heap_memory(device,
            &device->memory_allocator, &alloc_info, &heap->allocation)))
    {
        vkd3d_private_store_destroy(&heap->private_store);
        return hr;
    }

    heap->priority.allows_dynamic_residency = 
        device->device_info.pageable_device_memory_features.pageableDeviceLocalMemory &&
        heap->allocation.chunk == NULL /* not suballocated */ &&
        (device->memory_properties.memoryTypes[heap->allocation.device_allocation.vk_memory_type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkd3d_queue_timeline_trace_register_instantaneous(&device->queue_timeline_trace,
            VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_HEAP_ALLOCATION, desc->SizeInBytes);

    d3d_destruction_notifier_init(&heap->destruction_notifier, (IUnknown*)&heap->ID3D12Heap_iface);
    d3d12_device_add_ref(heap->device);
    return S_OK;
}

HRESULT d3d12_heap_create(struct d3d12_device *device, const D3D12_HEAP_DESC *desc,
        void* host_address, struct d3d12_heap **heap)
{
    struct d3d12_heap *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_heap_init(object, device, desc, host_address)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created heap %p.\n", object);

    *heap = object;
    return S_OK;
}
