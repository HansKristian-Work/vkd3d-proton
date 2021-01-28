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
static inline struct d3d12_heap_2 *impl_from_ID3D12Heap(d3d12_heap_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_heap_2, ID3D12Heap_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_QueryInterface(d3d12_heap_iface *iface,
        REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_ID3D12Heap)
            || IsEqualGUID(iid, &IID_ID3D12Heap1)
            || IsEqualGUID(iid, &IID_ID3D12Pageable)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12Heap_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_heap_AddRef(d3d12_heap_iface *iface)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);
    ULONG refcount = InterlockedIncrement(&heap->refcount);

    TRACE("%p increasing refcount to %u.\n", heap, refcount);
    return refcount;
}

static void d3d12_heap_destroy(struct d3d12_heap_2 *heap)
{
    TRACE("Destroying heap %p.\n", heap);

    vkd3d_free_memory_2(heap->device, &heap->device->memory_allocator, &heap->allocation);
    vkd3d_private_store_destroy(&heap->private_store);
    d3d12_device_release(heap->device);
    vkd3d_free(heap);
}

static ULONG STDMETHODCALLTYPE d3d12_heap_Release(d3d12_heap_iface *iface)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);
    ULONG refcount = InterlockedDecrement(&heap->refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
        d3d12_heap_destroy(heap);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_GetPrivateData(d3d12_heap_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetPrivateData(d3d12_heap_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetPrivateDataInterface(d3d12_heap_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&heap->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetName(d3d12_heap_iface *iface, const WCHAR *name)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name));

    if (!heap->allocation.chunk)
        return vkd3d_set_vk_object_name(heap->device, (uint64_t)heap->allocation.vk_memory,
                VK_OBJECT_TYPE_DEVICE_MEMORY, name);
    else
        return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_GetDevice(d3d12_heap_iface *iface, REFIID iid, void **device)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(heap->device, iid, device);
}

static D3D12_HEAP_DESC * STDMETHODCALLTYPE d3d12_heap_GetDesc(d3d12_heap_iface *iface,
        D3D12_HEAP_DESC *desc)
{
    struct d3d12_heap_2 *heap = impl_from_ID3D12Heap(iface);

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

static CONST_VTBL struct ID3D12Heap1Vtbl d3d12_heap_vtbl =
{
    /* IUnknown methods */
    d3d12_heap_QueryInterface,
    d3d12_heap_AddRef,
    d3d12_heap_Release,
    /* ID3D12Object methods */
    d3d12_heap_GetPrivateData,
    d3d12_heap_SetPrivateData,
    d3d12_heap_SetPrivateDataInterface,
    d3d12_heap_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_heap_GetDevice,
    /* ID3D12Heap methods */
    d3d12_heap_GetDesc,
    /* ID3D12Heap1 methods */
    d3d12_heap_GetProtectedResourceSession,
};

static struct d3d12_heap_2 *unsafe_impl_from_ID3D12Heap1(ID3D12Heap1 *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_heap_vtbl);
    return impl_from_ID3D12Heap(iface);
}

struct d3d12_heap_2 *unsafe_impl_from_ID3D12Heap_2(ID3D12Heap *iface)
{
    return unsafe_impl_from_ID3D12Heap1((ID3D12Heap1 *)iface);
}

static HRESULT validate_heap_desc(const D3D12_HEAP_DESC *desc)
{
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

    return S_OK;
}

static HRESULT d3d12_heap_init_2(struct d3d12_heap_2 *heap, struct d3d12_device *device,
        const D3D12_HEAP_DESC *desc, void* host_address)
{
    struct vkd3d_allocate_heap_memory_info alloc_info;
    HRESULT hr;

    memset(heap, 0, sizeof(*heap));
    heap->ID3D12Heap_iface.lpVtbl = &d3d12_heap_vtbl;
    heap->refcount = 1;
    heap->desc = *desc;
    heap->device = device;

    if (!heap->desc.Properties.CreationNodeMask)
        heap->desc.Properties.CreationNodeMask = 1;
    if (!heap->desc.Properties.VisibleNodeMask)
        heap->desc.Properties.VisibleNodeMask = 1;
    if (!heap->desc.Alignment)
        heap->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    if (FAILED(hr = validate_heap_desc(&heap->desc)))
        return hr;

    alloc_info.heap_desc = heap->desc;
    alloc_info.host_ptr = host_address;

    if (FAILED(hr = vkd3d_private_store_init(&heap->private_store)))
        return hr;

    if (FAILED(hr = vkd3d_allocate_heap_memory_2(device,
            &device->memory_allocator, &alloc_info, &heap->allocation)))
    {
        vkd3d_private_store_destroy(&heap->private_store);
        return hr;
    }

    d3d12_device_add_ref(heap->device);
    return S_OK;
}

HRESULT d3d12_heap_create_2(struct d3d12_device *device, const D3D12_HEAP_DESC *desc,
        void* host_address, struct d3d12_heap_2 **heap)
{
    struct d3d12_heap_2 *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_heap_init_2(object, device, desc, host_address)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created heap %p.\n", object);

    *heap = object;
    return S_OK;
}
