/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2019 Conor McCarthy for CodeWeavers
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

#include <float.h>

#include "vkd3d_private.h"
#include "hashmap.h"

#define VKD3D_NULL_SRV_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM
#define VKD3D_NULL_UAV_FORMAT DXGI_FORMAT_R32_UINT

static inline bool is_cpu_accessible_heap(const D3D12_HEAP_PROPERTIES *properties)
{
    if (properties->Type == D3D12_HEAP_TYPE_DEFAULT)
        return false;
    if (properties->Type == D3D12_HEAP_TYPE_CUSTOM)
    {
        return properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE
                || properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    }
    return true;
}

static uint32_t vkd3d_select_memory_types(struct d3d12_device *device, const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags)
{
    const VkPhysicalDeviceMemoryProperties *memory_info = &device->memory_properties;
    uint32_t type_mask = (1 << memory_info->memoryTypeCount) - 1;

    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_BUFFERS))
        type_mask &= device->memory_info.buffer_type_mask;

    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES))
        type_mask &= device->memory_info.sampled_type_mask;

    /* Render targets are not allowed on UPLOAD and READBACK heaps */
    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES) &&
            heap_properties->Type != D3D12_HEAP_TYPE_UPLOAD &&
            heap_properties->Type != D3D12_HEAP_TYPE_READBACK)
        type_mask &= device->memory_info.rt_ds_type_mask;

    if (!type_mask)
        ERR("No memory type found for heap flags %#x.\n", heap_flags);

    return type_mask;
}

static HRESULT vkd3d_select_memory_flags(struct d3d12_device *device, const D3D12_HEAP_PROPERTIES *heap_properties, VkMemoryPropertyFlags *type_flags)
{
    switch (heap_properties->Type)
    {
        case D3D12_HEAP_TYPE_DEFAULT:
            *type_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        case D3D12_HEAP_TYPE_UPLOAD:
            *type_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;

        case D3D12_HEAP_TYPE_READBACK:
            *type_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;

        case D3D12_HEAP_TYPE_CUSTOM:
            if (heap_properties->MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN
                    || (heap_properties->MemoryPoolPreference == D3D12_MEMORY_POOL_L1
                    && (is_cpu_accessible_heap(heap_properties) || d3d12_device_is_uma(device, NULL))))
            {
                WARN("Invalid memory pool preference.\n");
                return E_INVALIDARG;
            }

            switch (heap_properties->CPUPageProperty)
            {
                case D3D12_CPU_PAGE_PROPERTY_WRITE_BACK:
                    *type_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                    break;
                case D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE:
                    *type_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    break;
                case D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE:
                    *type_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    break;
                case D3D12_CPU_PAGE_PROPERTY_UNKNOWN:
                default:
                    WARN("Invalid CPU page property.\n");
                    return E_INVALIDARG;
            }
            break;

        default:
            WARN("Invalid heap type %#x.\n", heap_properties->Type);
            return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT vkd3d_try_allocate_memory(struct d3d12_device *device,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, VkDeviceMemory *vk_memory, uint32_t *vk_memory_type)
{
    const VkPhysicalDeviceMemoryProperties *memory_info = &device->memory_properties;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryAllocateInfo allocate_info;
    VkResult vr;
    uint32_t i;

    /* buffer_mask / sampled_mask etc will generally take care of this, but for certain fallback scenarios
     * where we select other memory types, we need to mask here as well. */
    type_mask &= device->memory_info.global_mask;

    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = pNext;
    allocate_info.allocationSize = size;

    for (i = 0; i < memory_info->memoryTypeCount; i++)
    {
        if (!(type_mask & (1u << i)))
            continue;

        if ((memory_info->memoryTypes[i].propertyFlags & type_flags) != type_flags)
            continue;

        allocate_info.memoryTypeIndex = i;

        if ((vr = VK_CALL(vkAllocateMemory(device->vk_device,
                &allocate_info, NULL, vk_memory))) == VK_SUCCESS)
        {
            if (vk_memory_type)
                *vk_memory_type = i;

            return S_OK;
        }
    }

    return E_OUTOFMEMORY;
}

static HRESULT vkd3d_allocate_memory(struct d3d12_device *device,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, VkDeviceMemory *vk_memory, uint32_t *vk_memory_type)
{
    const VkMemoryPropertyFlags optional_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    HRESULT hr;

    hr = vkd3d_try_allocate_memory(device, size, type_flags,
            type_mask, pNext, vk_memory, vk_memory_type);

    if (FAILED(hr) && (type_flags & optional_flags))
    {
        WARN("Memory allocation failed, falling back to system memory.\n");
        hr = vkd3d_try_allocate_memory(device, size, type_flags & ~optional_flags,
                type_mask, pNext, vk_memory, vk_memory_type);
    }

    return hr;
}

static HRESULT vkd3d_import_host_memory(struct d3d12_device *device,
        void *host_address, VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, VkDeviceMemory *vk_memory, uint32_t *vk_memory_type)
{
    VkImportMemoryHostPointerInfoEXT import_info;
    HRESULT hr;

    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
    import_info.pNext = pNext;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    import_info.pHostPointer = host_address;
    if (SUCCEEDED(hr = vkd3d_try_allocate_memory(device, size, type_flags, type_mask, &import_info, vk_memory, vk_memory_type)))
        return hr;

    /* If we failed, fall back to just being host visible / coherent (NVIDIA).
     * Generally the app will access the memory thorugh the main host pointer, so it's fine. */
    return vkd3d_try_allocate_memory(device, size,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            type_mask, &import_info, vk_memory, vk_memory_type);
}

static HRESULT vkd3d_allocate_device_memory(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        VkDeviceSize size, VkDeviceMemory *vk_memory, uint32_t *vk_memory_type)
{
    VkMemoryAllocateFlagsInfo flags_info;
    VkMemoryPropertyFlags type_flags;
    HRESULT hr;

    flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.pNext = NULL;
    flags_info.flags = 0;

    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_BUFFERS) &&
            device->device_info.buffer_device_address_features.bufferDeviceAddress)
        flags_info.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

    if (FAILED(hr = vkd3d_select_memory_flags(device, heap_properties, &type_flags)))
        return hr;

    if (FAILED(hr = vkd3d_allocate_memory(device, size, type_flags,
            vkd3d_select_memory_types(device, heap_properties, heap_flags),
            &flags_info, vk_memory, vk_memory_type)))
        return hr;

    return S_OK;
}

HRESULT vkd3d_allocate_buffer_memory(struct d3d12_device *device, VkBuffer vk_buffer, void *host_memory,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        VkDeviceMemory *vk_memory, uint32_t *vk_memory_type, VkDeviceSize *vk_memory_size)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryDedicatedRequirements dedicated_requirements;
    VkMemoryHostPointerPropertiesEXT host_properties;
    VkMemoryDedicatedAllocateInfo dedicated_info;
    VkMemoryRequirements2 memory_requirements2;
    VkMemoryRequirements *memory_requirements;
    VkBufferMemoryRequirementsInfo2 info;
    VkMemoryAllocateFlagsInfo flags_info;
    VkMemoryPropertyFlags type_flags;
    VkResult vr;
    HRESULT hr;

    memory_requirements = &memory_requirements2.memoryRequirements;

    info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    info.pNext = NULL;
    info.buffer = vk_buffer;

    dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    dedicated_requirements.pNext = NULL;

    memory_requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memory_requirements2.pNext = &dedicated_requirements;

    VK_CALL(vkGetBufferMemoryRequirements2(device->vk_device, &info, &memory_requirements2));

    if (host_memory)
    {
        if (((uintptr_t)host_memory) &
            (device->device_info.external_memory_host_properties.minImportedHostPointerAlignment - 1))
        {
            FIXME("Imported host memory is misaligned.\n");
            return E_INVALIDARG;
        }

        host_properties.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;
        host_properties.pNext = NULL;
        if (VK_CALL(vkGetMemoryHostPointerPropertiesEXT(device->vk_device,
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                host_memory, &host_properties)) != VK_SUCCESS)
            return E_INVALIDARG;

        memory_requirements->memoryTypeBits &= host_properties.memoryTypeBits;
    }

    if (heap_flags != D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS)
        memory_requirements->memoryTypeBits &= vkd3d_select_memory_types(device, heap_properties, heap_flags);

    flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.pNext = NULL;
    flags_info.flags = 0;

    if (device->device_info.buffer_device_address_features.bufferDeviceAddress)
        flags_info.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

    if (heap_flags == D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS && dedicated_requirements.prefersDedicatedAllocation)
    {
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.pNext = NULL;
        dedicated_info.image = VK_NULL_HANDLE;
        dedicated_info.buffer = vk_buffer;

        flags_info.pNext = &dedicated_info;
    }

    if (FAILED(hr = vkd3d_select_memory_flags(device, heap_properties, &type_flags)))
        return hr;

    if (host_memory)
    {
        if (FAILED(hr = vkd3d_import_host_memory(device, host_memory, memory_requirements->size, type_flags,
                memory_requirements->memoryTypeBits, &flags_info, vk_memory,
                vk_memory_type)))
        {
            return hr;
        }
    }
    else
    {
        if (FAILED(hr = vkd3d_allocate_memory(device, memory_requirements->size, type_flags,
                memory_requirements->memoryTypeBits, &flags_info, vk_memory,
                vk_memory_type)))
        {
            return hr;
        }
    }

    if ((vr = VK_CALL(vkBindBufferMemory(device->vk_device, vk_buffer, *vk_memory, 0))) < 0)
    {
        WARN("Failed to bind memory, vr %d.\n", vr);
        VK_CALL(vkFreeMemory(device->vk_device, *vk_memory, NULL));
        *vk_memory = VK_NULL_HANDLE;
    }

    if (vk_memory_size)
        *vk_memory_size = memory_requirements->size;

    return hresult_from_vk_result(vr);
}

static HRESULT vkd3d_allocate_image_memory(struct d3d12_device *device, VkImage vk_image,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        VkDeviceMemory *vk_memory, uint32_t *vk_memory_type, VkDeviceSize *vk_memory_size)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryDedicatedRequirements dedicated_requirements;
    VkMemoryDedicatedAllocateInfo dedicated_info;
    VkMemoryRequirements2 memory_requirements2;
    VkMemoryRequirements *memory_requirements;
    VkImageMemoryRequirementsInfo2 info;
    VkMemoryPropertyFlags type_flags;
    void *pNext = NULL;
    VkResult vr;
    HRESULT hr;

    memory_requirements = &memory_requirements2.memoryRequirements;

    info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    info.pNext = NULL;
    info.image = vk_image;

    dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    dedicated_requirements.pNext = NULL;

    memory_requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memory_requirements2.pNext = &dedicated_requirements;

    VK_CALL(vkGetImageMemoryRequirements2(device->vk_device, &info, &memory_requirements2));

    if (dedicated_requirements.prefersDedicatedAllocation)
    {
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.pNext = NULL;
        dedicated_info.image = vk_image;
        dedicated_info.buffer = VK_NULL_HANDLE;
        pNext = &dedicated_info;
    }

    if (FAILED(hr = vkd3d_select_memory_flags(device, heap_properties, &type_flags)))
        return hr;

    if (FAILED(hr = vkd3d_allocate_memory(device, memory_requirements->size, type_flags,
            memory_requirements->memoryTypeBits, pNext, vk_memory, vk_memory_type)))
        return hr;

    if ((vr = VK_CALL(vkBindImageMemory(device->vk_device, vk_image, *vk_memory, 0))) < 0)
    {
        WARN("Failed to bind memory, vr %d.\n", vr);
        VK_CALL(vkFreeMemory(device->vk_device, *vk_memory, NULL));
        *vk_memory = VK_NULL_HANDLE;
        return hresult_from_vk_result(vr);
    }

    if (vk_memory_size)
        *vk_memory_size = memory_requirements->size;

    return S_OK;
}

/* ID3D12Heap */
static inline struct d3d12_heap *impl_from_ID3D12Heap(d3d12_heap_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_heap, ID3D12Heap_iface);
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
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);
    ULONG refcount = InterlockedIncrement(&heap->refcount);

    TRACE("%p increasing refcount to %u.\n", heap, refcount);

    assert(!heap->is_private);

    return refcount;
}

static ULONG d3d12_resource_decref(struct d3d12_resource *resource);

static void d3d12_heap_cleanup(struct d3d12_heap *heap)
{
    struct d3d12_device *device = heap->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    if (heap->buffer_resource)
        d3d12_resource_decref(heap->buffer_resource);

    VK_CALL(vkFreeMemory(device->vk_device, heap->vk_memory, NULL));

    if (heap->is_private)
        device = NULL;

    if (device)
        d3d12_device_release(device);
}

static void d3d12_heap_destroy(struct d3d12_heap *heap)
{
    TRACE("Destroying heap %p.\n", heap);

    d3d12_heap_cleanup(heap);
    vkd3d_private_store_destroy(&heap->private_store);
    vkd3d_free(heap);
}

static ULONG STDMETHODCALLTYPE d3d12_heap_Release(d3d12_heap_iface *iface)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);
    ULONG refcount = InterlockedDecrement(&heap->refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
        d3d12_heap_destroy(heap);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_GetPrivateData(d3d12_heap_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetPrivateData(d3d12_heap_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetPrivateDataInterface(d3d12_heap_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&heap->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_SetName(d3d12_heap_iface *iface, const WCHAR *name)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, heap->device->wchar_size));

    return vkd3d_set_vk_object_name(heap->device, (uint64_t)heap->vk_memory,
            VK_OBJECT_TYPE_DEVICE_MEMORY, name);
}

static HRESULT STDMETHODCALLTYPE d3d12_heap_GetDevice(d3d12_heap_iface *iface, REFIID iid, void **device)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(heap->device, iid, device);
}

static D3D12_HEAP_DESC * STDMETHODCALLTYPE d3d12_heap_GetDesc(d3d12_heap_iface *iface,
        D3D12_HEAP_DESC *desc)
{
    struct d3d12_heap *heap = impl_from_ID3D12Heap(iface);

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

static struct d3d12_heap *unsafe_impl_from_ID3D12Heap1(ID3D12Heap1 *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_heap_vtbl);
    return impl_from_ID3D12Heap(iface);
}

struct d3d12_heap *unsafe_impl_from_ID3D12Heap(ID3D12Heap *iface)
{
    return unsafe_impl_from_ID3D12Heap1((ID3D12Heap1 *)iface);
}

static HRESULT validate_heap_desc(const D3D12_HEAP_DESC *desc, const struct d3d12_resource *resource)
{
    if (!resource && !desc->SizeInBytes)
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

    if (!resource && desc->Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY)
    {
        WARN("D3D12_HEAP_FLAG_ALLOW_DISPLAY is only for committed resources.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT validate_placed_resource_heap(struct d3d12_heap *heap, const D3D12_RESOURCE_DESC *resource_desc)
{
    D3D12_HEAP_FLAGS deny_flag;

    if (resource_desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        deny_flag = D3D12_HEAP_FLAG_DENY_BUFFERS;
    else if (resource_desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        deny_flag = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
    else
        deny_flag = D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

    if (heap->desc.Flags & deny_flag)
    {
        WARN("Cannot create placed resource on heap that denies resource category %#x.\n", deny_flag);
        return E_INVALIDARG;
    }

    if ((heap->desc.Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) &&
            !(resource_desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER))
    {
        ERR("Must declare ALLOW_CROSS_ADAPTER resource flag when heap is cross adapter.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT d3d12_resource_create(struct d3d12_device *device,
                                     const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
                                     const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
                                     const D3D12_CLEAR_VALUE *optimized_clear_value, bool placed,
                                     struct d3d12_resource **resource);

bool d3d12_heap_needs_host_barrier_for_write(struct d3d12_heap *heap)
{
    switch (heap->desc.Properties.Type)
    {
    case D3D12_HEAP_TYPE_DEFAULT:
    case D3D12_HEAP_TYPE_UPLOAD:
        return false;

    case D3D12_HEAP_TYPE_READBACK:
        return true;

    case D3D12_HEAP_TYPE_CUSTOM:
        return heap->desc.Properties.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;

    default:
        break;
    }

    return false;
}

static HRESULT d3d12_heap_init_omnipotent_buffer(struct d3d12_heap *heap, struct d3d12_device *device,
        const D3D12_HEAP_DESC *desc)
{
    D3D12_RESOURCE_STATES initial_resource_state;
    D3D12_RESOURCE_DESC resource_desc;
    HRESULT hr;

    /* Create a single omnipotent buffer which fills the entire heap.
     * Whenever we place buffer resources on this heap, we'll just offset this VkBuffer.
     * This allows us to keep VA space somewhat sane, and keeps number of (limited) VA allocations down.
     * One possible downside is that the buffer might be slightly slower to access,
     * but D3D12 has very lenient usage flags for buffers. */

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = desc->SizeInBytes;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (heap->desc.Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER)
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    switch (desc->Properties.Type)
    {
    case D3D12_HEAP_TYPE_UPLOAD:
        initial_resource_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;

    case D3D12_HEAP_TYPE_READBACK:
        initial_resource_state = D3D12_RESOURCE_STATE_COPY_DEST;
        break;

    default:
        /* Upload and readback heaps do not allow UAV access, only enable this flag for other heaps. */
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        initial_resource_state = D3D12_RESOURCE_STATE_COMMON;
        break;
    }

    if (FAILED(hr = d3d12_resource_create(device, &desc->Properties, desc->Flags,
            &resource_desc, initial_resource_state,
            NULL, false, &heap->buffer_resource)))
        return hr;

    /* This internal resource should not own a reference on the device.
     * d3d12_resource_create takes a reference on the device. */
    d3d12_device_release(device);
    return S_OK;
}

static HRESULT d3d12_heap_allocate_storage(struct d3d12_heap *heap,
        struct d3d12_device *device, const struct d3d12_resource *resource, void *host_memory)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const VkMemoryType *memory_type;
    VkDeviceSize vk_memory_size;
    VkResult vr;
    HRESULT hr;

    if (resource)
    {
        if (d3d12_resource_is_buffer(resource))
        {
            hr = vkd3d_allocate_buffer_memory(device, resource->vk_buffer, NULL,
                    &heap->desc.Properties, heap->desc.Flags | D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
                    &heap->vk_memory, &heap->vk_memory_type, &vk_memory_size);
        }
        else
        {
            hr = vkd3d_allocate_image_memory(device, resource->vk_image,
                    &heap->desc.Properties, heap->desc.Flags,
                    &heap->vk_memory, &heap->vk_memory_type, &vk_memory_size);
        }

        heap->desc.SizeInBytes = vk_memory_size;
    }
    else if (heap->buffer_resource)
    {
        hr = vkd3d_allocate_buffer_memory(device, heap->buffer_resource->vk_buffer, host_memory,
                &heap->desc.Properties, heap->desc.Flags,
                &heap->vk_memory, &heap->vk_memory_type, &vk_memory_size);
    }
    else
    {
        hr = vkd3d_allocate_device_memory(device, &heap->desc.Properties,
                heap->desc.Flags, heap->desc.SizeInBytes, &heap->vk_memory,
                &heap->vk_memory_type);
    }

    if (FAILED(hr))
        return hr;

    memory_type = &device->memory_properties.memoryTypes[heap->vk_memory_type];

    if (memory_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        if ((vr = VK_CALL(vkMapMemory(device->vk_device,
                                      heap->vk_memory, 0, VK_WHOLE_SIZE, 0, &heap->map_ptr))) < 0)
        {
            ERR("Failed to map memory, vr %d.\n", vr);
            return hresult_from_vk_result(hr);
        }

        if ((heap->desc.Flags & D3D12_HEAP_FLAG_SHARED) == 0)
        {
            /* Zero private host-visible memory */
            memset(heap->map_ptr, 0, heap->desc.SizeInBytes);
        }

        if (!(memory_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            VkMappedMemoryRange mapped_range;
            mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mapped_range.pNext = NULL;
            mapped_range.memory = heap->vk_memory;
            mapped_range.offset = 0;
            mapped_range.size = VK_WHOLE_SIZE;

            VK_CALL(vkFlushMappedMemoryRanges(device->vk_device, 1, &mapped_range));
        }
    }

    return S_OK;
}

static HRESULT d3d12_heap_init(struct d3d12_heap *heap,
        struct d3d12_device *device, const D3D12_HEAP_DESC *desc, const struct d3d12_resource *resource)
{
    bool buffers_allowed;
    HRESULT hr;

    memset(heap, 0, sizeof(*heap));
    heap->ID3D12Heap_iface.lpVtbl = &d3d12_heap_vtbl;
    heap->refcount = 1;
    heap->device = device;

    heap->is_private = !!resource;

    heap->desc = *desc;

    heap->map_ptr = NULL;
    heap->buffer_resource = NULL;

    if (!heap->is_private)
        d3d12_device_add_ref(heap->device);

    if (!heap->desc.Properties.CreationNodeMask)
        heap->desc.Properties.CreationNodeMask = 1;
    if (!heap->desc.Properties.VisibleNodeMask)
        heap->desc.Properties.VisibleNodeMask = 1;

    debug_ignored_node_mask(heap->desc.Properties.CreationNodeMask);
    debug_ignored_node_mask(heap->desc.Properties.VisibleNodeMask);

    if (!heap->desc.Alignment)
        heap->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    if (FAILED(hr = validate_heap_desc(&heap->desc, resource)))
    {
        d3d12_heap_cleanup(heap);
        return hr;
    }

    buffers_allowed = !(heap->desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS);
    if (buffers_allowed && !resource)
    {
        if (FAILED(hr = d3d12_heap_init_omnipotent_buffer(heap, device, desc)))
        {
            d3d12_heap_cleanup(heap);
            return hr;
        }
    }

    if (FAILED(hr = vkd3d_private_store_init(&heap->private_store)))
    {
        d3d12_heap_cleanup(heap);
        return hr;
    }

    if (FAILED(hr = d3d12_heap_allocate_storage(heap, device, resource, NULL)))
    {
        d3d12_heap_cleanup(heap);
        return hr;
    }

    return S_OK;
}

static HRESULT d3d12_heap_init_from_host_pointer(struct d3d12_heap *heap,
        struct d3d12_device *device, void *address, size_t size)
{
    HRESULT hr;

    if (!device->vk_info.EXT_external_memory_host)
    {
        WARN("VK_EXT_external_memory_host is not supported. Falling back to a private allocation. This will likely break debug code.\n");
        address = NULL;
    }

    memset(heap, 0, sizeof(*heap));
    heap->ID3D12Heap_iface.lpVtbl = &d3d12_heap_vtbl;
    heap->refcount = 1;
    heap->device = device;

    heap->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap->desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS |
            (address ? (D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) : 0);
    heap->desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap->desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    heap->desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap->desc.Properties.CreationNodeMask = 1;
    heap->desc.Properties.VisibleNodeMask = 1;
    heap->desc.SizeInBytes = size;

    d3d12_device_add_ref(heap->device);

    if (FAILED(hr = d3d12_heap_init_omnipotent_buffer(heap, device, &heap->desc)))
    {
        d3d12_heap_cleanup(heap);
        return hr;
    }

    if (FAILED(hr = vkd3d_private_store_init(&heap->private_store)))
    {
        d3d12_heap_cleanup(heap);
        return hr;
    }

    if (FAILED(hr = d3d12_heap_allocate_storage(heap, device, NULL, address)))
    {
        d3d12_heap_cleanup(heap);
        return hr;
    }

    return S_OK;
}

HRESULT d3d12_heap_create(struct d3d12_device *device, const D3D12_HEAP_DESC *desc,
        const struct d3d12_resource *resource, struct d3d12_heap **heap)
{
    struct d3d12_heap *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_heap_init(object, device, desc, resource)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created %s %p.\n", object->is_private ? "private heap" : "heap", object);

    *heap = object;

    return S_OK;
}

HRESULT d3d12_heap_create_from_host_pointer(struct d3d12_device *device, void *address, size_t size,
        struct d3d12_heap **heap)
{
    struct d3d12_heap *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_heap_init_from_host_pointer(object, device, address, size)))
    {
        vkd3d_free(object);
        return hr;
    }

    *heap = object;
    return S_OK;
}

static VkImageType vk_image_type_from_d3d12_resource_dimension(D3D12_RESOURCE_DIMENSION dimension)
{
    switch (dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            return VK_IMAGE_TYPE_1D;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            return VK_IMAGE_TYPE_2D;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            return VK_IMAGE_TYPE_3D;
        default:
            ERR("Invalid resource dimension %#x.\n", dimension);
            return VK_IMAGE_TYPE_2D;
    }
}

VkSampleCountFlagBits vk_samples_from_sample_count(unsigned int sample_count)
{
    switch (sample_count)
    {
        case 1:
            return VK_SAMPLE_COUNT_1_BIT;
        case 2:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16:
            return VK_SAMPLE_COUNT_16_BIT;
        case 32:
            return VK_SAMPLE_COUNT_32_BIT;
        case 64:
            return VK_SAMPLE_COUNT_64_BIT;
        default:
            return 0;
    }
}

VkSampleCountFlagBits vk_samples_from_dxgi_sample_desc(const DXGI_SAMPLE_DESC *desc)
{
    VkSampleCountFlagBits vk_samples;

    if ((vk_samples = vk_samples_from_sample_count(desc->Count)))
        return vk_samples;

    FIXME("Unhandled sample count %u.\n", desc->Count);
    return VK_SAMPLE_COUNT_1_BIT;
}

HRESULT vkd3d_create_buffer(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, VkBuffer *vk_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkExternalMemoryBufferCreateInfo external_info;
    const bool sparse_resource = !heap_properties;
    VkBufferCreateInfo buffer_info;
    D3D12_HEAP_TYPE heap_type;
    VkResult vr;

    heap_type = heap_properties ? heap_properties->Type : D3D12_HEAP_TYPE_DEFAULT;

    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.pNext = NULL;
    buffer_info.flags = 0;
    buffer_info.size = desc->Width;

    /* This is only used by OpenExistingHeapFrom*,
     * and external host memory is the only way for us to do CROSS_ADAPTER. */
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER)
    {
        external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        external_info.pNext = NULL;
        external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        buffer_info.pNext = &external_info;
    }

    if (sparse_resource)
    {
        buffer_info.flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT |
                VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
    }

    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT
            | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
            | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    if (device->vk_info.EXT_conditional_rendering)
        buffer_info.usage |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;

    if (heap_type == D3D12_HEAP_TYPE_DEFAULT && device->vk_info.EXT_transform_feedback)
    {
        buffer_info.usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT
                | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
    }

    if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
        buffer_info.usage &= ~VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    else if (heap_type == D3D12_HEAP_TYPE_READBACK)
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        buffer_info.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

        if (device->device_info.buffer_device_address_features.bufferDeviceAddress)
            buffer_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
    }

    if (!(desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
        buffer_info.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

    /* Buffers always have properties of D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS. */
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
    {
        WARN("D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS cannot be set for buffers.\n");
        return E_INVALIDARG;
    }

    if (device->queue_family_count > 1)
    {
        buffer_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        buffer_info.queueFamilyIndexCount = device->queue_family_count;
        buffer_info.pQueueFamilyIndices = device->queue_family_indices;
    }
    else
    {
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_info.queueFamilyIndexCount = 0;
        buffer_info.pQueueFamilyIndices = NULL;
    }

    if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        FIXME("Unsupported resource flags %#x.\n", desc->Flags);

    if ((vr = VK_CALL(vkCreateBuffer(device->vk_device, &buffer_info, NULL, vk_buffer))) < 0)
    {
        WARN("Failed to create Vulkan buffer, vr %d.\n", vr);
        *vk_buffer = VK_NULL_HANDLE;
    }

    return hresult_from_vk_result(vr);
}

static unsigned int max_miplevel_count(const D3D12_RESOURCE_DESC *desc)
{
    unsigned int size = max(desc->Width, desc->Height);
    size = max(size, d3d12_resource_desc_get_depth(desc, 0));
    return vkd3d_log2i(size) + 1;
}

static const struct vkd3d_format_compatibility_list *vkd3d_get_format_compatibility_list(
        const struct d3d12_device *device, DXGI_FORMAT dxgi_format)
{
    DXGI_FORMAT typeless_format;
    unsigned int i;

    if (!(typeless_format = vkd3d_get_typeless_format(device, dxgi_format)))
        typeless_format = dxgi_format;

    for (i = 0; i < device->format_compatibility_list_count; ++i)
    {
        if (device->format_compatibility_lists[i].typeless_format == typeless_format)
            return &device->format_compatibility_lists[i];
    }

    return NULL;
}

static bool vkd3d_is_linear_tiling_supported(const struct d3d12_device *device, VkImageCreateInfo *image_info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkImageFormatProperties properties;
    VkResult vr;

    if ((vr = VK_CALL(vkGetPhysicalDeviceImageFormatProperties(device->vk_physical_device, image_info->format,
            image_info->imageType, VK_IMAGE_TILING_LINEAR, image_info->usage, image_info->flags, &properties))) < 0)
    {
        if (vr != VK_ERROR_FORMAT_NOT_SUPPORTED)
            WARN("Failed to get device image format properties, vr %d.\n", vr);

        return false;
    }

    return image_info->extent.depth <= properties.maxExtent.depth
            && image_info->mipLevels <= properties.maxMipLevels
            && image_info->arrayLayers <= properties.maxArrayLayers
            && (image_info->samples & properties.sampleCounts);
}

static VkImageLayout vk_common_image_layout_from_d3d12_desc(const D3D12_RESOURCE_DESC *desc)
{
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        return VK_IMAGE_LAYOUT_GENERAL;

    /* DENY_SHADER_RESOURCE only allowed with ALLOW_DEPTH_STENCIL */
    if (desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static bool vkd3d_sparse_image_may_have_mip_tail(const D3D12_RESOURCE_DESC *desc,
        const VkSparseImageFormatProperties *sparse_info)
{
    VkExtent3D mip_extent, block_extent = sparse_info->imageGranularity;
    unsigned int mip_level;

    /* probe smallest mip level in the image */
    mip_level = desc->MipLevels - 1;
    mip_extent.width = d3d12_resource_desc_get_width(desc, mip_level);
    mip_extent.height = d3d12_resource_desc_get_height(desc, mip_level);
    mip_extent.depth = d3d12_resource_desc_get_depth(desc, mip_level);

    if (sparse_info->flags & VK_SPARSE_IMAGE_FORMAT_ALIGNED_MIP_SIZE_BIT)
    {
        return mip_extent.width % block_extent.width ||
                mip_extent.height % block_extent.height ||
                mip_extent.depth % block_extent.depth;
    }

    return mip_extent.width < block_extent.width ||
            mip_extent.height < block_extent.height ||
            mip_extent.depth < block_extent.depth;
}

static HRESULT vkd3d_create_image(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, struct d3d12_resource *resource, VkImage *vk_image)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct vkd3d_format_compatibility_list *compat_list;
    const bool sparse_resource = !heap_properties;
    VkImageFormatListCreateInfoKHR format_list;
    const struct vkd3d_format *format;
    VkImageCreateInfo image_info;
    DXGI_FORMAT typeless_format;
    unsigned int i;
    VkResult vr;

    if (!resource)
    {
        if (!(format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0)))
        {
            WARN("Invalid DXGI format %#x.\n", desc->Format);
            return E_INVALIDARG;
        }
    }
    else
    {
        format = resource->format;
    }

    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = NULL;
    image_info.flags = 0;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        /* Format compatibility rules are more relaxed for UAVs. */
        if (format->type != VKD3D_FORMAT_TYPE_UINT)
            image_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    }
    else if (!(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) && format->type == VKD3D_FORMAT_TYPE_TYPELESS)
    {
        image_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;

        if ((compat_list = vkd3d_get_format_compatibility_list(device, desc->Format)))
        {
            format_list.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
            format_list.pNext = NULL;
            format_list.viewFormatCount = compat_list->format_count;
            format_list.pViewFormats = compat_list->vk_formats;

            image_info.pNext = &format_list;
        }
    }
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D
            && desc->Width == desc->Height && desc->DepthOrArraySize >= 6
            && desc->SampleDesc.Count == 1)
        image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        image_info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT_KHR;

    if (sparse_resource)
    {
        image_info.flags |= VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                VK_IMAGE_CREATE_SPARSE_ALIASED_BIT;

        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
        {
            WARN("Tiled 1D textures not supported.\n");
            return E_INVALIDARG;
        }

        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D &&
                device->d3d12_caps.options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_3)
        {
            WARN("Tiled 3D textures not supported by device.\n");
            return E_INVALIDARG;
        }

        if (!is_power_of_two(format->vk_aspect_mask))
        {
            WARN("Multi-planar format %u not supported for tiled resources.\n", desc->Format);
            return E_INVALIDARG;
        }
    }

    image_info.imageType = vk_image_type_from_d3d12_resource_dimension(desc->Dimension);
    image_info.format = format->vk_format;
    image_info.extent.width = desc->Width;
    image_info.extent.height = desc->Height;

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        image_info.extent.depth = desc->DepthOrArraySize;
        image_info.arrayLayers = 1;
    }
    else
    {
        image_info.extent.depth = 1;
        image_info.arrayLayers = desc->DepthOrArraySize;
    }

    image_info.mipLevels = min(desc->MipLevels, max_miplevel_count(desc));
    image_info.samples = vk_samples_from_dxgi_sample_desc(&desc->SampleDesc);

    /* Additional usage flags for shader-based copies */
    typeless_format = vkd3d_get_typeless_format(device, format->dxgi_format);

    if (typeless_format == DXGI_FORMAT_R32_TYPELESS || typeless_format == DXGI_FORMAT_R16_TYPELESS)
    {
        image_info.usage |= (format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                ? VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if (sparse_resource)
    {
        if (desc->Layout != D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
        {
            WARN("D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE must be used for reserved texture.\n");
            return E_INVALIDARG;
        }

        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    }
    else if (desc->Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN)
    {
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    }
    else if (desc->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
    {
        image_info.tiling = VK_IMAGE_TILING_LINEAR;
    }
    else
    {
        FIXME("Unsupported layout %#x.\n", desc->Layout);
        return E_NOTIMPL;
    }

    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        image_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        image_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        image_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (!(desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
        image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    if ((desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) && device->queue_family_count > 1)
    {
        TRACE("Creating image with VK_SHARING_MODE_CONCURRENT.\n");
        image_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        image_info.queueFamilyIndexCount = device->queue_family_count;
        image_info.pQueueFamilyIndices = device->queue_family_indices;
    }
    else
    {
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.queueFamilyIndexCount = 0;
        image_info.pQueueFamilyIndices = NULL;
    }

    if (heap_properties && is_cpu_accessible_heap(heap_properties))
    {
        image_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

        if (vkd3d_is_linear_tiling_supported(device, &image_info))
        {
            /* Required for ReadFromSubresource(). */
            WARN("Forcing VK_IMAGE_TILING_LINEAR for CPU readable texture.\n");
            image_info.tiling = VK_IMAGE_TILING_LINEAR;
        }
    }
    else
    {
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (sparse_resource)
    {
        VkSparseImageFormatProperties sparse_infos[2];
        uint32_t sparse_info_count = ARRAY_SIZE(sparse_infos);

        // D3D12 only allows sparse images with one aspect, so we can only
        // get one struct for metadata aspect and one for the data aspect
        VK_CALL(vkGetPhysicalDeviceSparseImageFormatProperties(
                device->vk_physical_device, image_info.format,
                image_info.imageType, image_info.samples, image_info.usage,
                image_info.tiling, &sparse_info_count, sparse_infos));

        if (!sparse_info_count)
        {
            ERR("Sparse images not supported with format %u, type %u, samples %u, usage %#x, tiling %u.\n",
                    image_info.format, image_info.imageType, image_info.samples, image_info.usage, image_info.tiling);
            return E_INVALIDARG;
        }

        for (i = 0; i < sparse_info_count; i++)
        {
            if (sparse_infos[i].aspectMask & VK_IMAGE_ASPECT_METADATA_BIT)
                continue;

            if (vkd3d_sparse_image_may_have_mip_tail(desc, &sparse_infos[i]) && desc->DepthOrArraySize > 1 && desc->MipLevels > 1)
            {
                WARN("Multiple array layers not supported for sparse images with mip tail.\n");
                return E_INVALIDARG;
            }
        }
    }

    if (resource)
    {
        if (image_info.tiling == VK_IMAGE_TILING_LINEAR)
        {
            resource->flags |= VKD3D_RESOURCE_LINEAR_TILING;
            resource->common_layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        else
            resource->common_layout = vk_common_image_layout_from_d3d12_desc(desc);
    }

    if ((vr = VK_CALL(vkCreateImage(device->vk_device, &image_info, NULL, vk_image))) < 0)
        WARN("Failed to create Vulkan image, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

HRESULT vkd3d_get_image_allocation_info(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_ALLOCATION_INFO *allocation_info)
{
    static const D3D12_HEAP_PROPERTIES heap_properties = {D3D12_HEAP_TYPE_DEFAULT};
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    D3D12_RESOURCE_DESC validated_desc;
    VkMemoryRequirements requirements;
    VkImage vk_image;
    HRESULT hr;

    assert(desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER);
    assert(d3d12_resource_validate_desc(desc, device) == S_OK);

    if (!desc->MipLevels)
    {
        validated_desc = *desc;
        validated_desc.MipLevels = max_miplevel_count(desc);
        desc = &validated_desc;
    }

    /* XXX: We have to create an image to get its memory requirements. */
    if (SUCCEEDED(hr = vkd3d_create_image(device, &heap_properties, 0, desc, NULL, &vk_image)))
    {
        VK_CALL(vkGetImageMemoryRequirements(device->vk_device, vk_image, &requirements));
        VK_CALL(vkDestroyImage(device->vk_device, vk_image, NULL));

        allocation_info->SizeInBytes = requirements.size;
        allocation_info->Alignment = requirements.alignment;
    }

    return hr;
}

struct vkd3d_view_entry
{
    struct hash_map_entry entry;
    struct vkd3d_view_key key;
    struct vkd3d_view *view;
};

static bool d3d12_sampler_needs_border_color(D3D12_TEXTURE_ADDRESS_MODE u,
        D3D12_TEXTURE_ADDRESS_MODE v, D3D12_TEXTURE_ADDRESS_MODE w);

static uint32_t vkd3d_view_entry_hash(const void *key)
{
    const struct vkd3d_view_key *k = key;
    uint32_t hash;

    switch (k->view_type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
            hash = hash_uint64((uint64_t)k->u.buffer.buffer);
            hash = hash_combine(hash, hash_uint64(k->u.buffer.offset));
            hash = hash_combine(hash, hash_uint64(k->u.buffer.size));
            hash = hash_combine(hash, k->u.buffer.format->vk_format);
            break;

        case VKD3D_VIEW_TYPE_IMAGE:
            hash = hash_uint64((uint64_t)k->u.texture.image);
            hash = hash_combine(hash, k->u.texture.view_type);
            hash = hash_combine(hash, k->u.texture.layout);
            hash = hash_combine(hash, k->u.texture.format->vk_format);
            hash = hash_combine(hash, k->u.texture.miplevel_idx);
            hash = hash_combine(hash, k->u.texture.miplevel_count);
            hash = hash_combine(hash, k->u.texture.layer_idx);
            hash = hash_combine(hash, k->u.texture.layer_count);
            hash = hash_combine(hash, k->u.texture.components.r);
            hash = hash_combine(hash, k->u.texture.components.g);
            hash = hash_combine(hash, k->u.texture.components.b);
            hash = hash_combine(hash, k->u.texture.components.a);
            hash = hash_combine(hash, k->u.texture.allowed_swizzle);
            break;

        case VKD3D_VIEW_TYPE_SAMPLER:
            hash = (uint32_t)k->u.sampler.Filter;
            hash = hash_combine(hash, (uint32_t)k->u.sampler.AddressU);
            hash = hash_combine(hash, (uint32_t)k->u.sampler.AddressV);
            hash = hash_combine(hash, (uint32_t)k->u.sampler.AddressW);
            hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.MipLODBias));
            hash = hash_combine(hash, (uint32_t)k->u.sampler.MaxAnisotropy);
            hash = hash_combine(hash, (uint32_t)k->u.sampler.ComparisonFunc);
            if (d3d12_sampler_needs_border_color(k->u.sampler.AddressU, k->u.sampler.AddressV, k->u.sampler.AddressW))
            {
                hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.BorderColor[0]));
                hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.BorderColor[1]));
                hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.BorderColor[2]));
                hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.BorderColor[3]));
            }
            hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.MinLOD));
            hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.MaxLOD));
            break;

        default:
            ERR("Unexpected view type %d.\n", k->view_type);
            return 0;
    }

    return hash;
}

static bool vkd3d_view_entry_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_view_entry *e = (const struct vkd3d_view_entry*) entry;
    const struct vkd3d_view_key *k = key;

    if (k->view_type != e->key.view_type)
        return false;

    switch (k->view_type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
            return k->u.buffer.buffer == e->key.u.buffer.buffer &&
                    k->u.buffer.format == e->key.u.buffer.format &&
                    k->u.buffer.offset == e->key.u.buffer.offset &&
                    k->u.buffer.size == e->key.u.buffer.size;

        case VKD3D_VIEW_TYPE_IMAGE:
            return k->u.texture.image == e->key.u.texture.image &&
                    k->u.texture.view_type == e->key.u.texture.view_type &&
                    k->u.texture.layout == e->key.u.texture.layout &&
                    k->u.texture.format == e->key.u.texture.format &&
                    k->u.texture.miplevel_idx == e->key.u.texture.miplevel_idx &&
                    k->u.texture.miplevel_count == e->key.u.texture.miplevel_count &&
                    k->u.texture.layer_idx == e->key.u.texture.layer_idx &&
                    k->u.texture.layer_count == e->key.u.texture.layer_count &&
                    k->u.texture.components.r == e->key.u.texture.components.r &&
                    k->u.texture.components.g == e->key.u.texture.components.g &&
                    k->u.texture.components.b == e->key.u.texture.components.b &&
                    k->u.texture.components.a == e->key.u.texture.components.a &&
                    k->u.texture.allowed_swizzle == e->key.u.texture.allowed_swizzle;

        case VKD3D_VIEW_TYPE_SAMPLER:
            return k->u.sampler.Filter == e->key.u.sampler.Filter &&
                    k->u.sampler.AddressU == e->key.u.sampler.AddressU &&
                    k->u.sampler.AddressV == e->key.u.sampler.AddressV &&
                    k->u.sampler.AddressW == e->key.u.sampler.AddressW &&
                    k->u.sampler.MipLODBias == e->key.u.sampler.MipLODBias &&
                    k->u.sampler.MaxAnisotropy == e->key.u.sampler.MaxAnisotropy &&
                    k->u.sampler.ComparisonFunc == e->key.u.sampler.ComparisonFunc &&
                    (!d3d12_sampler_needs_border_color(k->u.sampler.AddressU, k->u.sampler.AddressV, k->u.sampler.AddressW) ||
                        (k->u.sampler.BorderColor[0] == e->key.u.sampler.BorderColor[0] &&
                        k->u.sampler.BorderColor[1] == e->key.u.sampler.BorderColor[1] &&
                        k->u.sampler.BorderColor[2] == e->key.u.sampler.BorderColor[2] &&
                        k->u.sampler.BorderColor[3] == e->key.u.sampler.BorderColor[3])) &&
                    k->u.sampler.MinLOD == e->key.u.sampler.MinLOD &&
                    k->u.sampler.MaxLOD == e->key.u.sampler.MaxLOD;
            break;

        default:
            ERR("Unexpected view type %d.\n", k->view_type);
            return false;
    }
}

HRESULT vkd3d_view_map_init(struct vkd3d_view_map *view_map)
{
    int rc;

    if ((rc = pthread_mutex_init(&view_map->mutex, NULL)))
        return hresult_from_errno(rc);

    hash_map_init(&view_map->map, &vkd3d_view_entry_hash, &vkd3d_view_entry_compare, sizeof(struct vkd3d_view_entry));
    return S_OK;
}

static void vkd3d_view_destroy(struct vkd3d_view *view, struct d3d12_device *device);

void vkd3d_view_map_destroy(struct vkd3d_view_map *view_map, struct d3d12_device *device)
{
    uint32_t i;

    for (i = 0; i < view_map->map.entry_count; i++)
    {
        struct vkd3d_view_entry *e = (struct vkd3d_view_entry *)hash_map_get_entry(&view_map->map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
            vkd3d_view_destroy(e->view, device);
    }

    hash_map_clear(&view_map->map);

    pthread_mutex_destroy(&view_map->mutex);
}

static struct vkd3d_view *vkd3d_view_create(enum vkd3d_view_type type);

static HRESULT d3d12_create_sampler(struct d3d12_device *device,
        const D3D12_SAMPLER_DESC *desc, VkSampler *vk_sampler);

struct vkd3d_view *vkd3d_view_map_create_view(struct vkd3d_view_map *view_map,
        struct d3d12_device *device, const struct vkd3d_view_key *key)
{
    struct vkd3d_view_entry entry, *e;
    struct vkd3d_view *view;
    bool success;
    int rc;

    if ((rc = pthread_mutex_lock(&view_map->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return NULL;
    }

    if ((e = (struct vkd3d_view_entry *)hash_map_find(&view_map->map, key)))
    {
        view = e->view;
        pthread_mutex_unlock(&view_map->mutex);
        return view;
    }

    switch (key->view_type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
            success = vkd3d_create_buffer_view(device, &key->u.buffer, &view);
            break;

        case VKD3D_VIEW_TYPE_IMAGE:
            success = vkd3d_create_texture_view(device, &key->u.texture, &view);
            break;

        case VKD3D_VIEW_TYPE_SAMPLER:
            success = (view = vkd3d_view_create(VKD3D_VIEW_TYPE_SAMPLER)) &&
                    SUCCEEDED(d3d12_create_sampler(device, &key->u.sampler, &view->vk_sampler));
            break;

        default:
            ERR("Unsupported view type %u.\n", key->view_type);
            return NULL;
    }

    if (!success)
    {
        pthread_mutex_unlock(&view_map->mutex);
        return NULL;
    }

    entry.key = *key;
    entry.view = view;

    if (!hash_map_insert(&view_map->map, key, &entry.entry))
        ERR("Failed to insert view into hash map.\n");

    pthread_mutex_unlock(&view_map->mutex);
    return view;
}

struct vkd3d_sampler_key
{
    D3D12_STATIC_SAMPLER_DESC desc;
};

struct vkd3d_sampler_entry
{
    struct hash_map_entry entry;
    D3D12_STATIC_SAMPLER_DESC desc;
    VkSampler vk_sampler;
};

static uint32_t vkd3d_sampler_entry_hash(const void *key)
{
    const struct vkd3d_sampler_key *k = key;
    uint32_t hash;

    hash = (uint32_t)k->desc.Filter;
    hash = hash_combine(hash, (uint32_t)k->desc.AddressU);
    hash = hash_combine(hash, (uint32_t)k->desc.AddressV);
    hash = hash_combine(hash, (uint32_t)k->desc.AddressW);
    hash = hash_combine(hash, float_bits_to_uint32(k->desc.MipLODBias));
    hash = hash_combine(hash, k->desc.MaxAnisotropy);
    hash = hash_combine(hash, (uint32_t)k->desc.ComparisonFunc);
    hash = hash_combine(hash, (uint32_t)k->desc.BorderColor);
    hash = hash_combine(hash, float_bits_to_uint32(k->desc.MinLOD));
    hash = hash_combine(hash, float_bits_to_uint32(k->desc.MaxLOD));
    return hash;
}

static bool vkd3d_sampler_entry_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_sampler_entry *e = (const struct vkd3d_sampler_entry*) entry;
    const struct vkd3d_sampler_key *k = key;

    return k->desc.Filter == e->desc.Filter &&
            k->desc.AddressU == e->desc.AddressU &&
            k->desc.AddressV == e->desc.AddressV &&
            k->desc.AddressW == e->desc.AddressW &&
            k->desc.MipLODBias == e->desc.MipLODBias &&
            k->desc.MaxAnisotropy == e->desc.MaxAnisotropy &&
            k->desc.ComparisonFunc == e->desc.ComparisonFunc &&
            k->desc.BorderColor == e->desc.BorderColor &&
            k->desc.MinLOD == e->desc.MinLOD &&
            k->desc.MaxLOD == e->desc.MaxLOD;
}

HRESULT vkd3d_sampler_state_init(struct vkd3d_sampler_state *state,
        struct d3d12_device *device)
{
    int rc;

    memset(state, 0, sizeof(*state));

    if ((rc = pthread_mutex_init(&state->mutex, NULL)))
        return hresult_from_errno(rc);

    hash_map_init(&state->map, &vkd3d_sampler_entry_hash, &vkd3d_sampler_entry_compare, sizeof(struct vkd3d_sampler_entry));
    return S_OK;
}

void vkd3d_sampler_state_cleanup(struct vkd3d_sampler_state *state,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    uint32_t i;

    for (i = 0; i < state->vk_descriptor_pool_count; i++)
        VK_CALL(vkDestroyDescriptorPool(device->vk_device, state->vk_descriptor_pools[i], NULL));

    vkd3d_free(state->vk_descriptor_pools);

    for (i = 0; i < state->map.entry_count; i++)
    {
        struct vkd3d_sampler_entry *e = (struct vkd3d_sampler_entry *)hash_map_get_entry(&state->map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
            VK_CALL(vkDestroySampler(device->vk_device, e->vk_sampler, NULL));
    }

    hash_map_clear(&state->map);

    pthread_mutex_destroy(&state->mutex);
}

HRESULT d3d12_create_static_sampler(struct d3d12_device *device,
        const D3D12_STATIC_SAMPLER_DESC *desc, VkSampler *vk_sampler);

HRESULT vkd3d_sampler_state_create_static_sampler(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, const D3D12_STATIC_SAMPLER_DESC *desc, VkSampler *vk_sampler)
{
    struct vkd3d_sampler_entry entry, *e;
    HRESULT hr;
    int rc;

    if ((rc = pthread_mutex_lock(&state->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if ((e = (struct vkd3d_sampler_entry*)hash_map_find(&state->map, desc)))
    {
        *vk_sampler = e->vk_sampler;
        pthread_mutex_unlock(&state->mutex);
        return S_OK;
    }

    if (FAILED(hr = d3d12_create_static_sampler(device, desc, vk_sampler)))
    {
        pthread_mutex_unlock(&state->mutex);
        return hr;
    }

    entry.desc = *desc;
    entry.vk_sampler = *vk_sampler;

    if (!hash_map_insert(&state->map, desc, &entry.entry))
        ERR("Failed to insert sampler into hash map.\n");

    pthread_mutex_unlock(&state->mutex);
    return S_OK;
}

static VkResult vkd3d_sampler_state_create_descriptor_pool(struct d3d12_device *device, VkDescriptorPool *vk_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorPoolCreateInfo pool_info;
    VkDescriptorPoolSize pool_size;

    pool_size.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    pool_size.descriptorCount = 16384;

    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 4096;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;

    return VK_CALL(vkCreateDescriptorPool(device->vk_device, &pool_info, NULL, vk_pool));
}

HRESULT vkd3d_sampler_state_allocate_descriptor_set(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, VkDescriptorSetLayout vk_layout, VkDescriptorSet *vk_set,
        VkDescriptorPool *vk_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkResult vr = VK_ERROR_OUT_OF_POOL_MEMORY;
    VkDescriptorSetAllocateInfo alloc_info;
    size_t i;
    int rc;

    if ((rc = pthread_mutex_lock(&state->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &vk_layout;

    for (i = 0; i < state->vk_descriptor_pool_count; i++)
    {
        alloc_info.descriptorPool = state->vk_descriptor_pools[i];
        vr = VK_CALL(vkAllocateDescriptorSets(device->vk_device, &alloc_info, vk_set));

        if (vr == VK_SUCCESS)
        {
            *vk_pool = alloc_info.descriptorPool;
            break;
        }
    }

    if (vr == VK_ERROR_OUT_OF_POOL_MEMORY || vr == VK_ERROR_FRAGMENTED_POOL)
    {
        vr = vkd3d_sampler_state_create_descriptor_pool(device, &alloc_info.descriptorPool);

        if (vr != VK_SUCCESS)
        {
            pthread_mutex_unlock(&state->mutex);
            return hresult_from_vk_result(vr);
        }

        if (!vkd3d_array_reserve((void **)&state->vk_descriptor_pools, &state->vk_descriptor_pools_size,
                state->vk_descriptor_pool_count + 1, sizeof(*state->vk_descriptor_pools)))
        {
            VK_CALL(vkDestroyDescriptorPool(device->vk_device, alloc_info.descriptorPool, NULL));
            pthread_mutex_unlock(&state->mutex);
            return E_OUTOFMEMORY;
        }

        state->vk_descriptor_pools[state->vk_descriptor_pool_count++] = alloc_info.descriptorPool;
        vr = VK_CALL(vkAllocateDescriptorSets(device->vk_device, &alloc_info, vk_set));
        *vk_pool = alloc_info.descriptorPool;
    }

    pthread_mutex_unlock(&state->mutex);
    return hresult_from_vk_result(vr);
}

void vkd3d_sampler_state_free_descriptor_set(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, VkDescriptorSet vk_set, VkDescriptorPool vk_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    int rc;

    if ((rc = pthread_mutex_lock(&state->mutex)))
        ERR("Failed to lock mutex, rc %d.\n", rc);

    if (vk_pool && vk_set)
        VK_CALL(vkFreeDescriptorSets(device->vk_device, vk_pool, 1, &vk_set));
    pthread_mutex_unlock(&state->mutex);
}

static void d3d12_resource_get_tiling(struct d3d12_device *device, struct d3d12_resource *resource,
        UINT *total_tile_count, D3D12_PACKED_MIP_INFO *packed_mip_info, D3D12_TILE_SHAPE *tile_shape,
        D3D12_SUBRESOURCE_TILING *tilings, VkSparseImageMemoryRequirements *vk_info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSparseImageMemoryRequirements *memory_requirements = NULL;
    unsigned int i, tile_count, packed_tiles, standard_mips;
    const D3D12_RESOURCE_DESC *desc = &resource->desc;
    uint32_t memory_requirement_count = 0;
    VkExtent3D block_extent;

    memset(vk_info, 0, sizeof(*vk_info));

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        tile_count = align(desc->Width, VKD3D_TILE_SIZE) / VKD3D_TILE_SIZE;

        packed_mip_info->NumStandardMips = 0;
        packed_mip_info->NumPackedMips = 0;
        packed_mip_info->NumTilesForPackedMips = 0;
        packed_mip_info->StartTileIndexInOverallResource = 0;

        tile_shape->WidthInTexels = VKD3D_TILE_SIZE;
        tile_shape->HeightInTexels = 1;
        tile_shape->DepthInTexels = 1;

        tilings[0].WidthInTiles = tile_count;
        tilings[0].HeightInTiles = 1;
        tilings[0].DepthInTiles = 1;
        tilings[0].StartTileIndexInOverallResource = 0;

        *total_tile_count = tile_count;
    }
    else
    {
        VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
                resource->vk_image, &memory_requirement_count, NULL));

        if (!memory_requirement_count)
        {
            ERR("Failed to query sparse memory requirements.\n");
            return;
        }

        memory_requirements = vkd3d_malloc(memory_requirement_count * sizeof(*memory_requirements));

        VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
                resource->vk_image, &memory_requirement_count, memory_requirements));

        for (i = 0; i < memory_requirement_count; i++)
        {
            if (!(memory_requirements[i].formatProperties.aspectMask & VK_IMAGE_ASPECT_METADATA_BIT))
                *vk_info = memory_requirements[i];
        }

        vkd3d_free(memory_requirements);

        /* Assume that there is no mip tail if either the size is zero or
         * if the first LOD is out of range. It's not clear what drivers
         * are supposed to report here if the image has no mip tail. */
        standard_mips = vk_info->imageMipTailSize
                ? min(desc->MipLevels, vk_info->imageMipTailFirstLod)
                : desc->MipLevels;

        packed_tiles = standard_mips < desc->MipLevels
                ? align(vk_info->imageMipTailSize, VKD3D_TILE_SIZE) / VKD3D_TILE_SIZE
                : 0;

        if (!(vk_info->formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT))
            packed_tiles *= d3d12_resource_desc_get_layer_count(desc);

        block_extent = vk_info->formatProperties.imageGranularity;
        tile_count = 0;

        for (i = 0; i < d3d12_resource_desc_get_sub_resource_count(desc); i++)
        {
            unsigned int mip_level = i % desc->MipLevels;
            unsigned int tile_count_w = align(d3d12_resource_desc_get_width(desc, mip_level), block_extent.width) / block_extent.width;
            unsigned int tile_count_h = align(d3d12_resource_desc_get_height(desc, mip_level), block_extent.height) / block_extent.height;
            unsigned int tile_count_d = align(d3d12_resource_desc_get_depth(desc, mip_level), block_extent.depth) / block_extent.depth;

            if (mip_level < standard_mips)
            {
                tilings[i].WidthInTiles = tile_count_w;
                tilings[i].HeightInTiles = tile_count_h;
                tilings[i].DepthInTiles = tile_count_d;
                tilings[i].StartTileIndexInOverallResource = tile_count;
                tile_count += tile_count_w * tile_count_h * tile_count_d;
            }
            else
            {
                tilings[i].WidthInTiles = 0;
                tilings[i].HeightInTiles = 0;
                tilings[i].DepthInTiles = 0;
                tilings[i].StartTileIndexInOverallResource = ~0u;
            }
        }

        packed_mip_info->NumStandardMips = standard_mips;
        packed_mip_info->NumTilesForPackedMips = packed_tiles;
        packed_mip_info->NumPackedMips = desc->MipLevels - standard_mips;
        packed_mip_info->StartTileIndexInOverallResource = packed_tiles ? tile_count : 0;

        tile_count += packed_tiles;

        if (standard_mips)
        {
            tile_shape->WidthInTexels = block_extent.width;
            tile_shape->HeightInTexels = block_extent.height;
            tile_shape->DepthInTexels = block_extent.depth;
        }
        else
        {
            tile_shape->WidthInTexels = 0;
            tile_shape->HeightInTexels = 0;
            tile_shape->DepthInTexels = 0;
        }

        *total_tile_count = tile_count;
    }
}

static void d3d12_resource_destroy(struct d3d12_resource *resource, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    vkd3d_view_map_destroy(&resource->view_map, resource->device);

    if (resource->flags & VKD3D_RESOURCE_EXTERNAL)
        return;

    if (resource->flags & VKD3D_RESOURCE_SPARSE)
    {
        VK_CALL(vkFreeMemory(device->vk_device, resource->sparse.vk_metadata_memory, NULL));

        vkd3d_free(resource->sparse.tiles);
        vkd3d_free(resource->sparse.tilings);
    }

    if (!(resource->flags & VKD3D_RESOURCE_PLACED_BUFFER))
    {
        if (resource->gpu_address)
            vkd3d_gpu_va_allocator_free(&device->gpu_va_allocator, resource->gpu_address);

        if (d3d12_resource_is_buffer(resource))
            VK_CALL(vkDestroyBuffer(device->vk_device, resource->vk_buffer, NULL));
        else
            VK_CALL(vkDestroyImage(device->vk_device, resource->vk_image, NULL));
    }

    if (resource->flags & VKD3D_RESOURCE_DEDICATED_HEAP)
        d3d12_heap_destroy(resource->heap);
}

static ULONG d3d12_resource_incref(struct d3d12_resource *resource)
{
    ULONG refcount = InterlockedIncrement(&resource->internal_refcount);

    TRACE("%p increasing refcount to %u.\n", resource, refcount);

    return refcount;
}

static ULONG d3d12_resource_decref(struct d3d12_resource *resource)
{
    ULONG refcount = InterlockedDecrement(&resource->internal_refcount);

    TRACE("%p decreasing refcount to %u.\n", resource, refcount);

    if (!refcount)
    {
        vkd3d_private_store_destroy(&resource->private_store);
        d3d12_resource_destroy(resource, resource->device);
        vkd3d_free(resource);
    }

    return refcount;
}

bool d3d12_resource_is_cpu_accessible(const struct d3d12_resource *resource)
{
    return resource->heap && is_cpu_accessible_heap(&resource->heap->desc.Properties);
}

static bool d3d12_resource_validate_box(const struct d3d12_resource *resource,
        unsigned int sub_resource_idx, const D3D12_BOX *box)
{
    unsigned int mip_level = sub_resource_idx % resource->desc.MipLevels;
    uint32_t width_mask, height_mask;
    uint64_t width, height, depth;

    width = d3d12_resource_desc_get_width(&resource->desc, mip_level);
    height = d3d12_resource_desc_get_height(&resource->desc, mip_level);
    depth = d3d12_resource_desc_get_depth(&resource->desc, mip_level);

    width_mask = resource->format->block_width - 1;
    height_mask = resource->format->block_height - 1;

    return box->left <= width && box->right <= width
            && box->top <= height && box->bottom <= height
            && box->front <= depth && box->back <= depth
            && !(box->left & width_mask)
            && !(box->right & width_mask)
            && !(box->top & height_mask)
            && !(box->bottom & height_mask);
}

static void d3d12_resource_get_level_box(const struct d3d12_resource *resource,
        unsigned int level, D3D12_BOX *box)
{
    box->left = 0;
    box->top = 0;
    box->front = 0;
    box->right = d3d12_resource_desc_get_width(&resource->desc, level);
    box->bottom = d3d12_resource_desc_get_height(&resource->desc, level);
    box->back = d3d12_resource_desc_get_depth(&resource->desc, level);
}

/* ID3D12Resource */
static inline struct d3d12_resource *impl_from_ID3D12Resource(d3d12_resource_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_resource, ID3D12Resource_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_QueryInterface(d3d12_resource_iface *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12Resource)
            || IsEqualGUID(riid, &IID_ID3D12Resource1)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Resource_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_resource_AddRef(d3d12_resource_iface *iface)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    ULONG refcount = InterlockedIncrement(&resource->refcount);

    TRACE("%p increasing refcount to %u.\n", resource, refcount);

    if (refcount == 1)
    {
        struct d3d12_device *device = resource->device;

        d3d12_device_add_ref(device);
        d3d12_resource_incref(resource);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_resource_Release(d3d12_resource_iface *iface)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    ULONG refcount = InterlockedDecrement(&resource->refcount);

    TRACE("%p decreasing refcount to %u.\n", resource, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = resource->device;

        d3d12_resource_decref(resource);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetPrivateData(d3d12_resource_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&resource->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_SetPrivateData(d3d12_resource_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&resource->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_SetPrivateDataInterface(d3d12_resource_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&resource->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_SetName(d3d12_resource_iface *iface, const WCHAR *name)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    HRESULT hr;

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, resource->device->wchar_size));

    if (resource->flags & VKD3D_RESOURCE_DEDICATED_HEAP)
    {
        if (FAILED(hr = d3d12_heap_SetName(&resource->heap->ID3D12Heap_iface, name)))
            return hr;
    }

    if (d3d12_resource_is_buffer(resource))
        return vkd3d_set_vk_object_name(resource->device, (uint64_t)resource->vk_buffer,
                VK_OBJECT_TYPE_BUFFER, name);
    else
        return vkd3d_set_vk_object_name(resource->device, (uint64_t)resource->vk_image,
                VK_OBJECT_TYPE_IMAGE, name);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetDevice(d3d12_resource_iface *iface, REFIID iid, void **device)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(resource->device, iid, device);
}

static bool d3d12_resource_get_mapped_memory_range(struct d3d12_resource *resource,
        UINT subresource, const D3D12_RANGE *range, VkMappedMemoryRange *vk_mapped_range)
{
    const struct d3d12_device *device = resource->device;
    const struct d3d12_heap *heap = resource->heap;

    if (range && range->End <= range->Begin)
        return false;

    if (device->memory_properties.memoryTypes[heap->vk_memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        return false;

    vk_mapped_range->sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    vk_mapped_range->pNext = NULL;
    vk_mapped_range->memory = heap->vk_memory;

    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        vk_mapped_range->offset = resource->heap_offset;
        vk_mapped_range->size = resource->desc.Width;
    }
    else
    {
        FIXME("Not implemented for textures.\n");
        return false;
    }

    if (range)
    {
        vk_mapped_range->offset += range->Begin;
        vk_mapped_range->size = range->End - range->Begin;
    }

    return true;
}

static void d3d12_resource_invalidate_range(struct d3d12_resource *resource,
        UINT subresource, const D3D12_RANGE *read_range)
{
    const struct vkd3d_vk_device_procs *vk_procs = &resource->device->vk_procs;
    VkMappedMemoryRange mapped_range;

    if (!d3d12_resource_get_mapped_memory_range(resource, subresource, read_range, &mapped_range))
        return;

    VK_CALL(vkInvalidateMappedMemoryRanges(resource->device->vk_device, 1, &mapped_range));
}

static void d3d12_resource_flush_range(struct d3d12_resource *resource,
        UINT subresource, const D3D12_RANGE *written_range)
{
    const struct vkd3d_vk_device_procs *vk_procs = &resource->device->vk_procs;
    VkMappedMemoryRange mapped_range;

    if (!d3d12_resource_get_mapped_memory_range(resource, subresource, written_range, &mapped_range))
        return;

    VK_CALL(vkFlushMappedMemoryRanges(resource->device->vk_device, 1, &mapped_range));
}

static void d3d12_resource_get_map_ptr(struct d3d12_resource *resource, void **data)
{
    assert(resource->heap->map_ptr);
    *data = (BYTE *)resource->heap->map_ptr + resource->heap_offset;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_Map(d3d12_resource_iface *iface, UINT sub_resource,
        const D3D12_RANGE *read_range, void **data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    unsigned int sub_resource_count;

    TRACE("iface %p, sub_resource %u, read_range %p, data %p.\n",
            iface, sub_resource, read_range, data);

    if (!d3d12_resource_is_cpu_accessible(resource))
    {
        WARN("Resource is not CPU accessible.\n");
        return E_INVALIDARG;
    }

    sub_resource_count = d3d12_resource_desc_get_sub_resource_count(&resource->desc);
    if (sub_resource >= sub_resource_count)
    {
        WARN("Sub-resource index %u is out of range (%u sub-resources).\n", sub_resource, sub_resource_count);
        return E_INVALIDARG;
    }

    if (d3d12_resource_is_texture(resource))
    {
        /* Textures seem to be mappable only on UMA adapters. */
        FIXME("Not implemented for textures.\n");
        return E_INVALIDARG;
    }

    if (!resource->heap)
    {
        FIXME("Not implemented for this resource type.\n");
        return E_NOTIMPL;
    }

    if (data)
    {
        d3d12_resource_get_map_ptr(resource, data);
        TRACE("Returning pointer %p.\n", *data);
    }

    d3d12_resource_invalidate_range(resource, sub_resource, read_range);
    return S_OK;
}

static void STDMETHODCALLTYPE d3d12_resource_Unmap(d3d12_resource_iface *iface, UINT sub_resource,
        const D3D12_RANGE *written_range)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    unsigned int sub_resource_count;

    TRACE("iface %p, sub_resource %u, written_range %p.\n",
            iface, sub_resource, written_range);

    sub_resource_count = d3d12_resource_desc_get_sub_resource_count(&resource->desc);
    if (sub_resource >= sub_resource_count)
    {
        WARN("Sub-resource index %u is out of range (%u sub-resources).\n", sub_resource, sub_resource_count);
        return;
    }

    d3d12_resource_flush_range(resource, sub_resource, written_range);
}

static D3D12_RESOURCE_DESC * STDMETHODCALLTYPE d3d12_resource_GetDesc(d3d12_resource_iface *iface,
        D3D12_RESOURCE_DESC *resource_desc)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);

    TRACE("iface %p, resource_desc %p.\n", iface, resource_desc);

    *resource_desc = resource->desc;
    return resource_desc;
}

static D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE d3d12_resource_GetGPUVirtualAddress(d3d12_resource_iface *iface)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);

    TRACE("iface %p.\n", iface);

    return resource->gpu_address;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_WriteToSubresource(d3d12_resource_iface *iface,
        UINT dst_sub_resource, const D3D12_BOX *dst_box, const void *src_data,
        UINT src_row_pitch, UINT src_slice_pitch)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    VkImageSubresource vk_sub_resource;
    VkSubresourceLayout vk_layout;
    struct d3d12_device *device;
    uint8_t *dst_data;
    D3D12_BOX box;

    TRACE("iface %p, src_data %p, src_row_pitch %u, src_slice_pitch %u, "
            "dst_sub_resource %u, dst_box %s.\n",
            iface, src_data, src_row_pitch, src_slice_pitch, dst_sub_resource, debug_d3d12_box(dst_box));

    if (d3d12_resource_is_buffer(resource))
    {
        WARN("Buffers are not supported.\n");
        return E_INVALIDARG;
    }

    device = resource->device;
    vk_procs = &device->vk_procs;

    if (resource->format->vk_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        FIXME("Not supported for format %#x.\n", resource->format->dxgi_format);
        return E_NOTIMPL;
    }

    vk_sub_resource.arrayLayer = dst_sub_resource / resource->desc.MipLevels;
    vk_sub_resource.mipLevel = dst_sub_resource % resource->desc.MipLevels;
    vk_sub_resource.aspectMask = resource->format->vk_aspect_mask;

    if (!dst_box)
    {
        d3d12_resource_get_level_box(resource, vk_sub_resource.mipLevel, &box);
        dst_box = &box;
    }
    else if (!d3d12_resource_validate_box(resource, dst_sub_resource, dst_box))
    {
        WARN("Invalid box %s.\n", debug_d3d12_box(dst_box));
        return E_INVALIDARG;
    }

    if (d3d12_box_is_empty(dst_box))
    {
        WARN("Empty box %s.\n", debug_d3d12_box(dst_box));
        return S_OK;
    }

    if (!d3d12_resource_is_cpu_accessible(resource))
    {
        FIXME_ONCE("Not implemented for this resource type.\n");
        return E_NOTIMPL;
    }
    if (!(resource->flags & VKD3D_RESOURCE_LINEAR_TILING))
    {
        FIXME_ONCE("Not implemented for image tiling other than VK_IMAGE_TILING_LINEAR.\n");
        return E_NOTIMPL;
    }

    VK_CALL(vkGetImageSubresourceLayout(device->vk_device, resource->vk_image, &vk_sub_resource, &vk_layout));
    TRACE("Offset %#"PRIx64", size %#"PRIx64", row pitch %#"PRIx64", depth pitch %#"PRIx64".\n",
            vk_layout.offset, vk_layout.size, vk_layout.rowPitch, vk_layout.depthPitch);

    d3d12_resource_get_map_ptr(resource, (void **)&dst_data);

    dst_data += vk_layout.offset + vkd3d_format_get_data_offset(resource->format, vk_layout.rowPitch,
            vk_layout.depthPitch, dst_box->left, dst_box->top, dst_box->front);

    vkd3d_format_copy_data(resource->format, src_data, src_row_pitch, src_slice_pitch,
            dst_data, vk_layout.rowPitch, vk_layout.depthPitch, dst_box->right - dst_box->left,
            dst_box->bottom - dst_box->top, dst_box->back - dst_box->front);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_ReadFromSubresource(d3d12_resource_iface *iface,
        void *dst_data, UINT dst_row_pitch, UINT dst_slice_pitch,
        UINT src_sub_resource, const D3D12_BOX *src_box)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    VkImageSubresource vk_sub_resource;
    VkSubresourceLayout vk_layout;
    struct d3d12_device *device;
    uint8_t *src_data;
    D3D12_BOX box;

    TRACE("iface %p, dst_data %p, dst_row_pitch %u, dst_slice_pitch %u, "
            "src_sub_resource %u, src_box %s.\n",
            iface, dst_data, dst_row_pitch, dst_slice_pitch, src_sub_resource, debug_d3d12_box(src_box));

    if (d3d12_resource_is_buffer(resource))
    {
        WARN("Buffers are not supported.\n");
        return E_INVALIDARG;
    }

    device = resource->device;
    vk_procs = &device->vk_procs;

    if (resource->format->vk_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        FIXME("Not supported for format %#x.\n", resource->format->dxgi_format);
        return E_NOTIMPL;
    }

    vk_sub_resource.arrayLayer = src_sub_resource / resource->desc.MipLevels;
    vk_sub_resource.mipLevel = src_sub_resource % resource->desc.MipLevels;
    vk_sub_resource.aspectMask = resource->format->vk_aspect_mask;

    if (!src_box)
    {
        d3d12_resource_get_level_box(resource, vk_sub_resource.mipLevel, &box);
        src_box = &box;
    }
    else if (!d3d12_resource_validate_box(resource, src_sub_resource, src_box))
    {
        WARN("Invalid box %s.\n", debug_d3d12_box(src_box));
        return E_INVALIDARG;
    }

    if (d3d12_box_is_empty(src_box))
    {
        WARN("Empty box %s.\n", debug_d3d12_box(src_box));
        return S_OK;
    }

    if (!d3d12_resource_is_cpu_accessible(resource))
    {
        FIXME_ONCE("Not implemented for this resource type.\n");
        return E_NOTIMPL;
    }
    if (!(resource->flags & VKD3D_RESOURCE_LINEAR_TILING))
    {
        FIXME_ONCE("Not implemented for image tiling other than VK_IMAGE_TILING_LINEAR.\n");
        return E_NOTIMPL;
    }

    VK_CALL(vkGetImageSubresourceLayout(device->vk_device, resource->vk_image, &vk_sub_resource, &vk_layout));
    TRACE("Offset %#"PRIx64", size %#"PRIx64", row pitch %#"PRIx64", depth pitch %#"PRIx64".\n",
            vk_layout.offset, vk_layout.size, vk_layout.rowPitch, vk_layout.depthPitch);

    d3d12_resource_get_map_ptr(resource, (void **)&src_data);

    src_data += vk_layout.offset + vkd3d_format_get_data_offset(resource->format, vk_layout.rowPitch,
            vk_layout.depthPitch, src_box->left, src_box->top, src_box->front);

    vkd3d_format_copy_data(resource->format, src_data, vk_layout.rowPitch, vk_layout.depthPitch,
            dst_data, dst_row_pitch, dst_slice_pitch, src_box->right - src_box->left,
            src_box->bottom - src_box->top, src_box->back - src_box->front);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetHeapProperties(d3d12_resource_iface *iface,
        D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS *flags)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource(iface);
    struct d3d12_heap *heap;

    TRACE("iface %p, heap_properties %p, flags %p.\n",
            iface, heap_properties, flags);

    if (resource->flags & VKD3D_RESOURCE_EXTERNAL)
    {
        if (heap_properties)
        {
            memset(heap_properties, 0, sizeof(*heap_properties));
            heap_properties->Type = D3D12_HEAP_TYPE_DEFAULT;
            heap_properties->CreationNodeMask = 1;
            heap_properties->VisibleNodeMask = 1;
        }
        if (flags)
            *flags = D3D12_HEAP_FLAG_NONE;
        return S_OK;
    }

    if (!(heap = resource->heap))
    {
        WARN("Cannot get heap properties for reserved resources.\n");
        return E_INVALIDARG;
    }

    if (heap_properties)
        *heap_properties = heap->desc.Properties;
    if (flags)
        *flags = heap->desc.Flags;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetProtectedResourceSession(d3d12_resource_iface *iface,
        REFIID iid, void **protected_session)
{
    FIXME("iface %p, iid %s, protected_session %p stub!", iface, debugstr_guid(iid), protected_session);

    return E_NOTIMPL;
}

static CONST_VTBL struct ID3D12Resource1Vtbl d3d12_resource_vtbl =
{
    /* IUnknown methods */
    d3d12_resource_QueryInterface,
    d3d12_resource_AddRef,
    d3d12_resource_Release,
    /* ID3D12Object methods */
    d3d12_resource_GetPrivateData,
    d3d12_resource_SetPrivateData,
    d3d12_resource_SetPrivateDataInterface,
    d3d12_resource_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_resource_GetDevice,
    /* ID3D12Resource methods */
    d3d12_resource_Map,
    d3d12_resource_Unmap,
    d3d12_resource_GetDesc,
    d3d12_resource_GetGPUVirtualAddress,
    d3d12_resource_WriteToSubresource,
    d3d12_resource_ReadFromSubresource,
    d3d12_resource_GetHeapProperties,
    /* ID3D12Resource1 methods */
    d3d12_resource_GetProtectedResourceSession,
};

static struct d3d12_resource *unsafe_impl_from_ID3D12Resource1(ID3D12Resource1 *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_resource_vtbl);
    return impl_from_ID3D12Resource(iface);
}

struct d3d12_resource *unsafe_impl_from_ID3D12Resource(ID3D12Resource *iface)
{
    return unsafe_impl_from_ID3D12Resource1((ID3D12Resource1 *)iface);
}

VkImageSubresource d3d12_resource_get_vk_subresource(const struct d3d12_resource *resource, uint32_t subresource_idx, bool all_aspects)
{
    uint32_t layer_count = d3d12_resource_desc_get_layer_count(&resource->desc);
    VkImageSubresource subresource;

    subresource.aspectMask = resource->format->vk_aspect_mask;
    subresource.mipLevel = subresource_idx % resource->desc.MipLevels;
    subresource.arrayLayer = (subresource_idx / resource->desc.MipLevels) % layer_count;

    if (!all_aspects)
    {
        /* For all formats we currently handle, the n-th aspect bit in Vulkan
         * corresponds to the n-th plane in D3D12, so isolate the respective
         * bit in the aspect mask. */
        uint32_t i, plane_idx = subresource_idx / d3d12_resource_desc_get_sub_resource_count(&resource->desc);

        for (i = 0; i < plane_idx; i++)
            subresource.aspectMask &= (subresource.aspectMask - 1);

        subresource.aspectMask &= -subresource.aspectMask;
    }

    return subresource;
}

static void d3d12_validate_resource_flags(D3D12_RESOURCE_FLAGS flags)
{
    unsigned int unknown_flags = flags & ~(D3D12_RESOURCE_FLAG_NONE
            | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
            | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
            | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
            | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE
            | D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER
            | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);

    if (unknown_flags)
        FIXME("Unknown resource flags %#x.\n", unknown_flags);
}

static bool d3d12_resource_validate_texture_format(const D3D12_RESOURCE_DESC *desc,
        const struct vkd3d_format *format)
{
    if (!vkd3d_format_is_compressed(format))
        return true;

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D && format->block_height > 1)
    {
        WARN("1D texture with a format block height > 1.\n");
        return false;
    }

    if (align(desc->Width, format->block_width) != desc->Width
            || align(desc->Height, format->block_height) != desc->Height)
    {
        WARN("Invalid size %"PRIu64"x%u for block compressed format %#x.\n",
                desc->Width, desc->Height, desc->Format);
        return false;
    }

    return true;
}

static bool d3d12_resource_validate_texture_alignment(const D3D12_RESOURCE_DESC *desc,
        const struct vkd3d_format *format)
{
    uint64_t estimated_size;

    if (!desc->Alignment)
        return true;

    if (desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
            && desc->Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
            && (desc->SampleDesc.Count == 1 || desc->Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT))
    {
        WARN("Invalid resource alignment %#"PRIx64".\n", desc->Alignment);
        return false;
    }

    if (desc->Alignment < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
    {
        /* Windows uses the slice size to determine small alignment eligibility. DepthOrArraySize is ignored. */
        estimated_size = desc->Width * desc->Height * format->byte_count * format->block_byte_count
                / (format->block_width * format->block_height);
        if (estimated_size > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
        {
            WARN("Invalid resource alignment %#"PRIx64" (required %#x).\n",
                    desc->Alignment, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
            return false;
        }
    }

    /* The size check for MSAA textures with D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT is probably
     * not important. The 4MB requirement is no longer universal and Vulkan has no such requirement. */

    return true;
}

HRESULT d3d12_resource_validate_desc(const D3D12_RESOURCE_DESC *desc, struct d3d12_device *device)
{
    const struct vkd3d_format *format;

    switch (desc->Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            if (desc->MipLevels != 1)
            {
                WARN("Invalid miplevel count %u for buffer.\n", desc->MipLevels);
                return E_INVALIDARG;
            }

            if (desc->Format != DXGI_FORMAT_UNKNOWN || desc->Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR
                    || desc->Height != 1 || desc->DepthOrArraySize != 1
                    || desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality != 0
                    || (desc->Alignment != 0 && desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT))
            {
                WARN("Invalid parameters for a buffer resource.\n");
                return E_INVALIDARG;
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            if (desc->Height != 1)
            {
                WARN("1D texture with a height of %u.\n", desc->Height);
                return E_INVALIDARG;
            }
            /* Fall through. */
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (!(format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0)))
            {
                WARN("Invalid format %#x.\n", desc->Format);
                return E_INVALIDARG;
            }

            if (!d3d12_resource_validate_texture_format(desc, format)
                    || !d3d12_resource_validate_texture_alignment(desc, format))
                return E_INVALIDARG;
            break;

        default:
            WARN("Invalid resource dimension %#x.\n", desc->Dimension);
            return E_INVALIDARG;
    }

    d3d12_validate_resource_flags(desc->Flags);

    return S_OK;
}

static bool d3d12_resource_validate_heap_properties(const struct d3d12_resource *resource,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_RESOURCE_STATES initial_state)
{
    if (heap_properties->Type == D3D12_HEAP_TYPE_UPLOAD
            || heap_properties->Type == D3D12_HEAP_TYPE_READBACK)
    {
        if (d3d12_resource_is_texture(resource))
        {
            WARN("Textures cannot be created on upload/readback heaps.\n");
            return false;
        }

        if (resource->desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        {
            WARN("Render target and unordered access buffers cannot be created on upload/readback heaps.\n");
            return false;
        }
    }

    if (heap_properties->Type == D3D12_HEAP_TYPE_UPLOAD && initial_state != D3D12_RESOURCE_STATE_GENERIC_READ)
    {
        WARN("For D3D12_HEAP_TYPE_UPLOAD the state must be D3D12_RESOURCE_STATE_GENERIC_READ.\n");
        return false;
    }
    if (heap_properties->Type == D3D12_HEAP_TYPE_READBACK && initial_state != D3D12_RESOURCE_STATE_COPY_DEST)
    {
        WARN("For D3D12_HEAP_TYPE_READBACK the state must be D3D12_RESOURCE_STATE_COPY_DEST.\n");
        return false;
    }

    return true;
}

static HRESULT d3d12_resource_bind_sparse_metadata(struct d3d12_resource *resource,
        struct d3d12_device *device, struct d3d12_sparse_info *sparse)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSparseImageMemoryRequirements *sparse_requirements = NULL;
    VkSparseImageOpaqueMemoryBindInfo opaque_bind;
    VkMemoryRequirements memory_requirements;
    VkSparseMemoryBind *memory_binds = NULL;
    struct vkd3d_queue *vkd3d_queue = NULL;
    uint32_t sparse_requirement_count;
    VkQueue vk_queue = VK_NULL_HANDLE;
    unsigned int i, j, k, bind_count;
    VkBindSparseInfo bind_info;
    VkDeviceSize metadata_size;
    HRESULT hr = S_OK;
    VkResult vr;

    if (d3d12_resource_is_buffer(resource))
        return S_OK;

    /* We expect the metadata aspect for image resources to be uncommon on most
     * drivers, so most of the time we'll just return early. The implementation
     * is therefore aimed at simplicity, and not very well tested in practice. */
    VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
        resource->vk_image, &sparse_requirement_count, NULL));

    if (!(sparse_requirements = vkd3d_malloc(sparse_requirement_count * sizeof(*sparse_requirements))))
    {
        ERR("Failed to allocate sparse memory requirement array.\n");
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }

    VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
        resource->vk_image, &sparse_requirement_count, sparse_requirements));

    /* Find out how much memory and how many bind infos we need */
    metadata_size = 0;
    bind_count = 0;

    for (i = 0; i < sparse_requirement_count; i++)
    {
        const VkSparseImageMemoryRequirements *req = &sparse_requirements[i];

        if (req->formatProperties.aspectMask & VK_IMAGE_ASPECT_METADATA_BIT)
        {
            uint32_t layer_count = 1;

            if (!(req->formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT))
                layer_count = d3d12_resource_desc_get_layer_count(&resource->desc);

            metadata_size *= layer_count * req->imageMipTailSize;
            bind_count += layer_count;
        }
    }

    if (!metadata_size)
        goto cleanup;

    /* Allocate memory for metadata mip tail */
    TRACE("Allocating sparse metadata for resource %p.\n", resource);

    VK_CALL(vkGetImageMemoryRequirements(device->vk_device, resource->vk_image, &memory_requirements));

    if ((vr = vkd3d_allocate_memory(device, metadata_size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            memory_requirements.memoryTypeBits, NULL, &sparse->vk_metadata_memory, NULL)))
    {
        ERR("Failed to allocate device memory for sparse metadata, vr %d.\n", vr);
        hr = hresult_from_vk_result(vr);
        goto cleanup;
    }

    /* Fill in opaque memory bind info */
    if (!(memory_binds = vkd3d_malloc(bind_count * sizeof(*memory_binds))))
    {
        ERR("Failed to allocate sparse memory bind info array.\n");
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }

    metadata_size = 0;

    for (i = 0, j = 0; i < sparse_requirement_count; i++)
    {
        const VkSparseImageMemoryRequirements *req = &sparse_requirements[i];

        if (req->formatProperties.aspectMask & VK_IMAGE_ASPECT_METADATA_BIT)
        {
            uint32_t layer_count = 1;

            if (!(req->formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT))
                layer_count = d3d12_resource_desc_get_layer_count(&resource->desc);

            for (k = 0; k < layer_count; k++)
            {
                VkSparseMemoryBind *bind = &memory_binds[j++];
                bind->resourceOffset = req->imageMipTailOffset + req->imageMipTailStride * k;
                bind->size = req->imageMipTailSize;
                bind->memory = sparse->vk_metadata_memory;
                bind->memoryOffset = metadata_size;
                bind->flags = VK_SPARSE_MEMORY_BIND_METADATA_BIT;

                metadata_size += req->imageMipTailSize;
            }
        }
    }

    /* Bind metadata memory to the image */
    opaque_bind.image = resource->vk_image;
    opaque_bind.bindCount = bind_count;
    opaque_bind.pBinds = memory_binds;

    bind_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
    bind_info.pNext = NULL;
    bind_info.waitSemaphoreCount = 0;
    bind_info.pWaitSemaphores = NULL;
    bind_info.bufferBindCount = 0;
    bind_info.pBufferBinds = NULL;
    bind_info.imageOpaqueBindCount = 1;
    bind_info.pImageOpaqueBinds = &opaque_bind;
    bind_info.imageBindCount = 0;
    bind_info.pImageBinds = NULL;
    bind_info.signalSemaphoreCount = 0;
    bind_info.pSignalSemaphores = NULL;

    vkd3d_queue = device->queues[VKD3D_QUEUE_FAMILY_SPARSE_BINDING];

    if (!(vk_queue = vkd3d_queue_acquire(vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", vkd3d_queue);
        goto cleanup;
    }

    if ((vr = VK_CALL(vkQueueBindSparse(vk_queue, 1, &bind_info, VK_NULL_HANDLE))) < 0)
    {
        ERR("Failed to bind sparse metadata to image, vr %d.\n", vr);
        hr = hresult_from_vk_result(vr);
        goto cleanup;
    }

    /* The application is free to use or destroy the resource
     * immediately after creation, so we need to wait for the
     * sparse binding operation to finish on the GPU. */
    if ((vr = VK_CALL(vkQueueWaitIdle(vk_queue))))
    {
        ERR("Failed to wait for sparse binding to complete.\n");
        hr = hresult_from_vk_result(vr);
    }

cleanup:
    if (vkd3d_queue && vk_queue)
        vkd3d_queue_release(vkd3d_queue);

    vkd3d_free(sparse_requirements);
    vkd3d_free(memory_binds);
    return hr;
}

static HRESULT d3d12_resource_init_sparse_info(struct d3d12_resource *resource,
        struct d3d12_device *device, struct d3d12_sparse_info *sparse)
{
    VkSparseImageMemoryRequirements vk_memory_requirements;
    unsigned int i, subresource;
    VkOffset3D tile_offset;
    HRESULT hr;

    memset(sparse, 0, sizeof(*sparse));

    if (!(resource->flags & VKD3D_RESOURCE_SPARSE))
        return S_OK;

    sparse->tiling_count = d3d12_resource_desc_get_sub_resource_count(&resource->desc);
    sparse->tile_count = 0;

    if (!(sparse->tilings = vkd3d_malloc(sparse->tiling_count * sizeof(*sparse->tilings))))
    {
        ERR("Failed to allocate subresource tiling info array.\n");
        return E_OUTOFMEMORY;
    }

    d3d12_resource_get_tiling(device, resource, &sparse->tile_count, &sparse->packed_mips,
            &sparse->tile_shape, sparse->tilings, &vk_memory_requirements);

    if (!(sparse->tiles = vkd3d_malloc(sparse->tile_count * sizeof(*sparse->tiles))))
    {
        ERR("Failed to allocate tile mapping array.\n");
        return E_OUTOFMEMORY;
    }

    tile_offset.x = 0;
    tile_offset.y = 0;
    tile_offset.z = 0;
    subresource = 0;

    for (i = 0; i < sparse->tile_count; i++)
    {
        if (d3d12_resource_is_buffer(resource))
        {
            VkDeviceSize offset = VKD3D_TILE_SIZE * i;
            sparse->tiles[i].buffer.offset = offset;
            sparse->tiles[i].buffer.length = min(VKD3D_TILE_SIZE, resource->desc.Width - offset);
        }
        else if (sparse->packed_mips.NumPackedMips && i >= sparse->packed_mips.StartTileIndexInOverallResource)
        {
            VkDeviceSize offset = VKD3D_TILE_SIZE * (i - sparse->packed_mips.StartTileIndexInOverallResource);
            sparse->tiles[i].buffer.offset = vk_memory_requirements.imageMipTailOffset + offset;
            sparse->tiles[i].buffer.length = min(VKD3D_TILE_SIZE, vk_memory_requirements.imageMipTailSize - offset);
        }
        else
        {
            struct d3d12_sparse_image_region *region = &sparse->tiles[i].image;
            VkExtent3D block_extent = vk_memory_requirements.formatProperties.imageGranularity;
            VkExtent3D mip_extent;

            assert(subresource < sparse->tiling_count && sparse->tilings[subresource].WidthInTiles &&
                    sparse->tilings[subresource].HeightInTiles && sparse->tilings[subresource].DepthInTiles);

            region->subresource.aspectMask = vk_memory_requirements.formatProperties.aspectMask;
            region->subresource.mipLevel = subresource % resource->desc.MipLevels;
            region->subresource.arrayLayer = subresource / resource->desc.MipLevels;

            region->offset.x = tile_offset.x * block_extent.width;
            region->offset.y = tile_offset.y * block_extent.height;
            region->offset.z = tile_offset.z * block_extent.depth;

            mip_extent.width = d3d12_resource_desc_get_width(&resource->desc, region->subresource.mipLevel);
            mip_extent.height = d3d12_resource_desc_get_height(&resource->desc, region->subresource.mipLevel);
            mip_extent.depth = d3d12_resource_desc_get_depth(&resource->desc, region->subresource.mipLevel);

            region->extent.width = min(block_extent.width, mip_extent.width - region->offset.x);
            region->extent.height = min(block_extent.height, mip_extent.height - region->offset.y);
            region->extent.depth = min(block_extent.depth, mip_extent.depth - region->offset.z);

            if (++tile_offset.x == (int32_t)sparse->tilings[subresource].WidthInTiles)
            {
                tile_offset.x = 0;
                if (++tile_offset.y == (int32_t)sparse->tilings[subresource].HeightInTiles)
                {
                    tile_offset.y = 0;
                    if (++tile_offset.z == (int32_t)sparse->tilings[subresource].DepthInTiles)
                    {
                        tile_offset.z = 0;

                        /* Find next subresource that is not part of the packed mip tail */
                        while ((++subresource % resource->desc.MipLevels) >= sparse->packed_mips.NumStandardMips)
                            continue;
                    }
                }
            }
        }

        sparse->tiles[i].vk_memory = VK_NULL_HANDLE;
        sparse->tiles[i].vk_offset = 0;
    }

    if (FAILED(hr = d3d12_resource_bind_sparse_metadata(resource, device, sparse)))
        return hr;

    return S_OK;
}

static LONG64 global_cookie_counter;

static HRESULT d3d12_resource_init(struct d3d12_resource *resource, struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, bool placed)
{
    HRESULT hr;

    resource->ID3D12Resource_iface.lpVtbl = &d3d12_resource_vtbl;
    resource->refcount = 1;
    resource->internal_refcount = 1;

    resource->desc = *desc;
    resource->cookie = InterlockedIncrement64(&global_cookie_counter);

    if (FAILED(hr = vkd3d_view_map_init(&resource->view_map)))
        return hr;

    if (heap_properties && !d3d12_resource_validate_heap_properties(resource, heap_properties, initial_state))
        return E_INVALIDARG;

    if (!is_valid_resource_state(initial_state))
    {
        WARN("Invalid initial resource state %#x.\n", initial_state);
        return E_INVALIDARG;
    }

    if (optimized_clear_value && d3d12_resource_is_buffer(resource))
    {
        WARN("Optimized clear value must be NULL for buffers.\n");
        return E_INVALIDARG;
    }

    if (optimized_clear_value)
        WARN("Ignoring optimized clear value.\n");

    resource->gpu_address = 0;
    resource->flags = 0;
    resource->initial_layout_transition = 0;
    resource->common_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (placed && d3d12_resource_is_buffer(resource))
        resource->flags |= VKD3D_RESOURCE_PLACED_BUFFER;

    if (!heap_properties)
        resource->flags |= VKD3D_RESOURCE_SPARSE;

    if (FAILED(hr = d3d12_resource_validate_desc(&resource->desc, device)))
        return hr;

    resource->format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0);

    switch (desc->Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            /* We'll inherit a VkBuffer reference from the heap with an implied offset. */
            if (placed)
            {
                resource->vk_buffer = VK_NULL_HANDLE;
                break;
            }

            if (FAILED(hr = vkd3d_create_buffer(device, heap_properties, heap_flags,
                    &resource->desc, &resource->vk_buffer)))
                return hr;
            if (!(resource->gpu_address = vkd3d_gpu_va_allocator_allocate(&device->gpu_va_allocator,
                    desc->Alignment ? desc->Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                    desc->Width, resource)))
            {
                ERR("Failed to allocate GPU VA.\n");
                d3d12_resource_destroy(resource, device);
                return E_OUTOFMEMORY;
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (!resource->desc.MipLevels)
                resource->desc.MipLevels = max_miplevel_count(desc);
            resource->initial_layout_transition = 1;
            if (FAILED(hr = vkd3d_create_image(device, heap_properties, heap_flags,
                    &resource->desc, resource, &resource->vk_image)))
                return hr;
            break;

        default:
            WARN("Invalid resource dimension %#x.\n", resource->desc.Dimension);
            return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_resource_init_sparse_info(resource, device, &resource->sparse)))
    {
        d3d12_resource_destroy(resource, device);
        return hr;
    }

    resource->heap = NULL;
    resource->heap_offset = 0;

    if (FAILED(hr = vkd3d_private_store_init(&resource->private_store)))
    {
        d3d12_resource_destroy(resource, device);
        return hr;
    }

    d3d12_device_add_ref(resource->device = device);

    return S_OK;
}

static HRESULT d3d12_resource_create(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, bool placed, struct d3d12_resource **resource)
{
    struct d3d12_resource *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_resource_init(object, device, heap_properties, heap_flags,
            desc, initial_state, optimized_clear_value, placed)))
    {
        vkd3d_free(object);
        return hr;
    }

    *resource = object;

    return hr;
}

static HRESULT vkd3d_allocate_resource_memory(
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags)
{
    D3D12_HEAP_DESC heap_desc;
    HRESULT hr;

    heap_desc.SizeInBytes = 0;
    heap_desc.Properties = *heap_properties;
    heap_desc.Alignment = 0;
    heap_desc.Flags = heap_flags;
    if (SUCCEEDED(hr = d3d12_heap_create(device, &heap_desc, resource, &resource->heap)))
        resource->flags |= VKD3D_RESOURCE_DEDICATED_HEAP;
    return hr;
}

HRESULT d3d12_committed_resource_create(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource)
{
    struct d3d12_resource *object;
    HRESULT hr;

    if (!heap_properties)
    {
        WARN("Heap properties are NULL.\n");
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_resource_create(device, heap_properties, heap_flags,
            desc, initial_state, optimized_clear_value, false, &object)))
        return hr;

    if (FAILED(hr = vkd3d_allocate_resource_memory(device, object, heap_properties, heap_flags)))
    {
        d3d12_resource_Release(&object->ID3D12Resource_iface);
        return hr;
    }

    TRACE("Created committed resource %p.\n", object);

    *resource = object;

    return S_OK;
}

static HRESULT vkd3d_bind_heap_memory(struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_heap *heap, uint64_t heap_offset)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDevice vk_device = device->vk_device;
    VkMemoryRequirements requirements;
    VkResult vr;

    if (resource->flags & VKD3D_RESOURCE_PLACED_BUFFER)
    {
        /* Just inherit the buffer from the heap. */
        resource->vk_buffer = heap->buffer_resource->vk_buffer;
        resource->heap = heap;
        resource->heap_offset = heap_offset;
        resource->gpu_address = heap->buffer_resource->gpu_address + heap_offset;
        return S_OK;
    }

    if (d3d12_resource_is_buffer(resource))
        VK_CALL(vkGetBufferMemoryRequirements(vk_device, resource->vk_buffer, &requirements));
    else
        VK_CALL(vkGetImageMemoryRequirements(vk_device, resource->vk_image, &requirements));

    if (heap_offset % requirements.alignment)
    {
        FIXME("Invalid heap offset %#"PRIx64" (alignment %#"PRIx64").\n",
                heap_offset, requirements.alignment);
        goto allocate_memory;
    }

    if (!(requirements.memoryTypeBits & (1u << heap->vk_memory_type)))
    {
        FIXME("Memory type %u cannot be bound to resource %p (allowed types %#x).\n",
                heap->vk_memory_type, resource, requirements.memoryTypeBits);
        goto allocate_memory;
    }

    if (d3d12_resource_is_buffer(resource))
        vr = VK_CALL(vkBindBufferMemory(vk_device, resource->vk_buffer, heap->vk_memory, heap_offset));
    else
        vr = VK_CALL(vkBindImageMemory(vk_device, resource->vk_image, heap->vk_memory, heap_offset));

    if (vr == VK_SUCCESS)
    {
        resource->heap = heap;
        resource->heap_offset = heap_offset;
    }
    else
    {
        WARN("Failed to bind memory, vr %d.\n", vr);
    }

    return hresult_from_vk_result(vr);

allocate_memory:
    FIXME("Allocating device memory.\n");
    return vkd3d_allocate_resource_memory(device, resource, &heap->desc.Properties, heap->desc.Flags);
}

HRESULT d3d12_placed_resource_create(struct d3d12_device *device, struct d3d12_heap *heap, uint64_t heap_offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource)
{
    struct d3d12_resource *object;
    HRESULT hr;

    if (FAILED(hr = validate_placed_resource_heap(heap, desc)))
        return hr;

    if (FAILED(hr = d3d12_resource_create(device, &heap->desc.Properties, heap->desc.Flags,
            desc, initial_state, optimized_clear_value, true, &object)))
        return hr;

    if (FAILED(hr = vkd3d_bind_heap_memory(device, object, heap, heap_offset)))
    {
        d3d12_resource_Release(&object->ID3D12Resource_iface);
        return hr;
    }

    TRACE("Created placed resource %p.\n", object);

    *resource = object;

    return S_OK;
}

HRESULT d3d12_reserved_resource_create(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource)
{
    struct d3d12_resource *object;
    HRESULT hr;

    if (FAILED(hr = d3d12_resource_create(device, NULL, 0,
            desc, initial_state, optimized_clear_value, false, &object)))
        return hr;

    TRACE("Created reserved resource %p.\n", object);

    *resource = object;

    return S_OK;
}

VKD3D_EXPORT HRESULT vkd3d_create_image_resource(ID3D12Device *device,
        const struct vkd3d_image_resource_create_info *create_info, ID3D12Resource **resource)
{
    struct d3d12_device *d3d12_device = unsafe_impl_from_ID3D12Device((d3d12_device_iface *)device);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("device %p, create_info %p, resource %p.\n", device, create_info, resource);

    if (!create_info || !resource)
        return E_INVALIDARG;
    if (create_info->type != VKD3D_STRUCTURE_TYPE_IMAGE_RESOURCE_CREATE_INFO)
    {
        WARN("Invalid structure type %#x.\n", create_info->type);
        return E_INVALIDARG;
    }
    if (create_info->next)
        WARN("Unhandled next %p.\n", create_info->next);

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));

    object->ID3D12Resource_iface.lpVtbl = &d3d12_resource_vtbl;
    object->refcount = 1;
    object->internal_refcount = 1;
    object->desc = create_info->desc;
    object->vk_image = create_info->vk_image;
    object->flags = create_info->flags;
    object->flags |= VKD3D_RESOURCE_EXTERNAL;
    object->initial_layout_transition = 1;
    object->common_layout = vk_common_image_layout_from_d3d12_desc(&object->desc);

    memset(&object->sparse, 0, sizeof(object->sparse));

    object->format = vkd3d_format_from_d3d12_resource_desc(d3d12_device, &create_info->desc, 0);

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    d3d12_device_add_ref(object->device = d3d12_device);

    TRACE("Created resource %p.\n", object);

    *resource = (ID3D12Resource *)&object->ID3D12Resource_iface;

    return S_OK;
}

VKD3D_EXPORT ULONG vkd3d_resource_incref(ID3D12Resource *resource)
{
    TRACE("resource %p.\n", resource);
    return d3d12_resource_incref(unsafe_impl_from_ID3D12Resource(resource));
}

VKD3D_EXPORT ULONG vkd3d_resource_decref(ID3D12Resource *resource)
{
    TRACE("resource %p.\n", resource);
    return d3d12_resource_decref(unsafe_impl_from_ID3D12Resource(resource));
}

/* CBVs, SRVs, UAVs */
static struct vkd3d_view *vkd3d_view_create(enum vkd3d_view_type type)
{
    struct vkd3d_view *view;

    if ((view = vkd3d_malloc(sizeof(*view))))
    {
        view->refcount = 1;
        view->type = type;
        view->cookie = InterlockedIncrement64(&global_cookie_counter);
    }
    return view;
}

void vkd3d_view_incref(struct vkd3d_view *view)
{
    InterlockedIncrement(&view->refcount);
}

static void vkd3d_view_destroy(struct vkd3d_view *view, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("Destroying view %p.\n", view);

    switch (view->type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
            VK_CALL(vkDestroyBufferView(device->vk_device, view->vk_buffer_view, NULL));
            break;
        case VKD3D_VIEW_TYPE_IMAGE:
            VK_CALL(vkDestroyImageView(device->vk_device, view->vk_image_view, NULL));
            break;
        case VKD3D_VIEW_TYPE_SAMPLER:
            VK_CALL(vkDestroySampler(device->vk_device, view->vk_sampler, NULL));
            break;
        default:
            WARN("Unhandled view type %d.\n", view->type);
    }

    vkd3d_free(view);
}

void vkd3d_view_decref(struct vkd3d_view *view, struct d3d12_device *device)
{
    if (!InterlockedDecrement(&view->refcount))
        vkd3d_view_destroy(view, device);
}

void d3d12_desc_copy(struct d3d12_desc *dst, struct d3d12_desc *src,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_descriptor_data metadata = src->metadata;
    VkCopyDescriptorSet vk_copies[2], *vk_copy;
    uint32_t copy_count = 0;
    bool needs_update;

    /* Only update the descriptor if something has changed */
    if (!(needs_update = (metadata.cookie != dst->metadata.cookie)))
    {
        /* We don't have a cookie for the UAV counter, so just force update if we have that.
         * If flags differ, we also need to update. E.g. happens if UAV counter flag is turned off.
         * We have no cookie for the UAV counter itself.
         * Lastly, if we have plain VkBuffers, offset/range might differ. */
        if ((metadata.flags & VKD3D_DESCRIPTOR_FLAG_UAV_COUNTER) != 0 ||
            (metadata.flags != dst->metadata.flags))
        {
            needs_update = true;
        }
        else if ((metadata.flags & VKD3D_DESCRIPTOR_FLAG_DEFINED) != 0 &&
                (metadata.flags & VKD3D_DESCRIPTOR_FLAG_VIEW) == 0)
        {
            needs_update =
                    dst->info.buffer.offset != src->info.buffer.offset ||
                    dst->info.buffer.range != src->info.buffer.range;
        }
    }

    if (needs_update)
    {
        dst->metadata = metadata;
        dst->info = src->info;

        if (metadata.flags & VKD3D_DESCRIPTOR_FLAG_DEFINED)
        {
            vk_copy = &vk_copies[copy_count++];
            vk_copy->sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
            vk_copy->pNext = NULL;
            vk_copy->srcSet = src->heap->vk_descriptor_sets[metadata.set_index];
            vk_copy->srcBinding = 0;
            vk_copy->srcArrayElement = src->heap_offset;
            vk_copy->dstSet = dst->heap->vk_descriptor_sets[metadata.set_index];
            vk_copy->dstBinding = 0;
            vk_copy->dstArrayElement = dst->heap_offset;
            vk_copy->descriptorCount = 1;
        }

        if (metadata.flags & VKD3D_DESCRIPTOR_FLAG_UAV_COUNTER)
        {
            if (dst->heap->uav_counters.data)
            {
                dst->heap->uav_counters.data[dst->heap_offset] = src->counter_address;
                dst->counter_address = src->counter_address;
            }
            else
            {
                uint32_t set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
                        VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_COUNTER);

                vk_copy = &vk_copies[copy_count++];
                vk_copy->sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
                vk_copy->pNext = NULL;
                vk_copy->srcSet = src->heap->vk_descriptor_sets[set_index];
                vk_copy->srcBinding = 0;
                vk_copy->srcArrayElement = src->heap_offset;
                vk_copy->dstSet = dst->heap->vk_descriptor_sets[set_index];
                vk_copy->dstBinding = 0;
                vk_copy->dstArrayElement = dst->heap_offset;
                vk_copy->descriptorCount = 1;
            }
        }

        if (copy_count)
            VK_CALL(vkUpdateDescriptorSets(device->vk_device, 0, NULL, copy_count, vk_copies));
    }
}

static VkDeviceSize vkd3d_get_required_texel_buffer_alignment(const struct d3d12_device *device,
        const struct vkd3d_format *format)
{
    const VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *properties;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    VkDeviceSize alignment;

    if (vk_info->EXT_texel_buffer_alignment)
    {
        properties = &vk_info->texel_buffer_alignment_properties;

        alignment = max(properties->storageTexelBufferOffsetAlignmentBytes,
                properties->uniformTexelBufferOffsetAlignmentBytes);

        if (properties->storageTexelBufferOffsetSingleTexelAlignment
                && properties->uniformTexelBufferOffsetSingleTexelAlignment)
        {
            assert(!vkd3d_format_is_compressed(format));
            return min(format->byte_count, alignment);
        }

        return alignment;
    }

    return vk_info->device_limits.minTexelBufferOffsetAlignment;
}

static bool vkd3d_create_vk_buffer_view(struct d3d12_device *device,
        VkBuffer vk_buffer, const struct vkd3d_format *format,
        VkDeviceSize offset, VkDeviceSize range, VkBufferView *vk_view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkBufferViewCreateInfo view_desc;
    VkDeviceSize alignment;
    VkResult vr;

    if (vkd3d_format_is_compressed(format))
    {
        WARN("Invalid format for buffer view %#x.\n", format->dxgi_format);
        return false;
    }

    alignment = vkd3d_get_required_texel_buffer_alignment(device, format);
    if (offset % alignment)
        FIXME("Offset %#"PRIx64" violates the required alignment %#"PRIx64".\n", offset, alignment);

    view_desc.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    view_desc.pNext = NULL;
    view_desc.flags = 0;
    view_desc.buffer = vk_buffer;
    view_desc.format = format->vk_format;
    view_desc.offset = offset;
    view_desc.range = range;
    if ((vr = VK_CALL(vkCreateBufferView(device->vk_device, &view_desc, NULL, vk_view))) < 0)
        WARN("Failed to create Vulkan buffer view, vr %d.\n", vr);
    return vr == VK_SUCCESS;
}

bool vkd3d_create_buffer_view(struct d3d12_device *device, const struct vkd3d_buffer_view_desc *desc, struct vkd3d_view **view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_view *object;
    VkBufferView vk_view;

    if (!vkd3d_create_vk_buffer_view(device, desc->buffer, desc->format, desc->offset, desc->size, &vk_view))
        return false;

    if (!(object = vkd3d_view_create(VKD3D_VIEW_TYPE_BUFFER)))
    {
        VK_CALL(vkDestroyBufferView(device->vk_device, vk_view, NULL));
        return false;
    }

    object->vk_buffer_view = vk_view;
    object->format = desc->format;
    object->info.buffer.offset = desc->offset;
    object->info.buffer.size = desc->size;
    *view = object;
    return true;
}

#define VKD3D_VIEW_RAW_BUFFER 0x1

static bool vkd3d_create_buffer_view_for_resource(struct d3d12_device *device,
        struct d3d12_resource *resource, DXGI_FORMAT view_format,
        unsigned int offset, unsigned int size, unsigned int structure_stride,
        unsigned int flags, struct vkd3d_view **view)
{
    const struct vkd3d_format *format;
    struct vkd3d_view_key key;
    VkDeviceSize element_size;

    if (view_format == DXGI_FORMAT_R32_TYPELESS && (flags & VKD3D_VIEW_RAW_BUFFER))
    {
        format = vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);
        element_size = format->byte_count;
    }
    else if (view_format == DXGI_FORMAT_UNKNOWN && structure_stride)
    {
        format = vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);
        element_size = structure_stride;
    }
    else if ((format = vkd3d_format_from_d3d12_resource_desc(device, &resource->desc, view_format)))
    {
        element_size = format->byte_count;
    }
    else
    {
        WARN("Failed to find format for %#x.\n", resource->desc.Format);
        return false;
    }

    assert(d3d12_resource_is_buffer(resource));

    key.view_type = VKD3D_VIEW_TYPE_BUFFER;
    key.u.buffer.buffer = resource->vk_buffer;
    key.u.buffer.format = format;
    key.u.buffer.offset = resource->heap_offset + offset * element_size;
    key.u.buffer.size = size * element_size;

    return !!(*view = vkd3d_view_map_create_view(&resource->view_map, device, &key));
}

static void vkd3d_set_view_swizzle_for_format(VkComponentMapping *components,
        const struct vkd3d_format *format, bool allowed_swizzle)
{
    components->r = VK_COMPONENT_SWIZZLE_R;
    components->g = VK_COMPONENT_SWIZZLE_G;
    components->b = VK_COMPONENT_SWIZZLE_B;
    components->a = VK_COMPONENT_SWIZZLE_A;

    if (format->vk_aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        if (allowed_swizzle)
        {
            components->r = VK_COMPONENT_SWIZZLE_ZERO;
            components->g = VK_COMPONENT_SWIZZLE_R;
            components->b = VK_COMPONENT_SWIZZLE_ZERO;
            components->a = VK_COMPONENT_SWIZZLE_ZERO;
        }
        else
        {
            FIXME("Stencil swizzle is not supported for format %#x.\n",
                    format->dxgi_format);
        }
    }

    if (format->dxgi_format == DXGI_FORMAT_A8_UNORM)
    {
        if (allowed_swizzle)
        {
            components->r = VK_COMPONENT_SWIZZLE_ZERO;
            components->g = VK_COMPONENT_SWIZZLE_ZERO;
            components->b = VK_COMPONENT_SWIZZLE_ZERO;
            components->a = VK_COMPONENT_SWIZZLE_R;
        }
        else
        {
            FIXME("Alpha swizzle is not supported.\n");
        }
    }

    if (format->dxgi_format == DXGI_FORMAT_B8G8R8X8_UNORM
            || format->dxgi_format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
    {
        if (allowed_swizzle)
        {
            components->r = VK_COMPONENT_SWIZZLE_R;
            components->g = VK_COMPONENT_SWIZZLE_G;
            components->b = VK_COMPONENT_SWIZZLE_B;
            components->a = VK_COMPONENT_SWIZZLE_ONE;
        }
        else
        {
            FIXME("B8G8R8X8 swizzle is not supported.\n");
        }
    }
}

static VkComponentSwizzle vk_component_swizzle_from_d3d12(unsigned int component_mapping,
        unsigned int component_index)
{
    D3D12_SHADER_COMPONENT_MAPPING mapping
            = D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(component_index, component_mapping);

    switch (mapping)
    {
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0:
            return VK_COMPONENT_SWIZZLE_R;
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1:
            return VK_COMPONENT_SWIZZLE_G;
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2:
            return VK_COMPONENT_SWIZZLE_B;
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3:
            return VK_COMPONENT_SWIZZLE_A;
        case D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0:
            return VK_COMPONENT_SWIZZLE_ZERO;
        case D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1:
            return VK_COMPONENT_SWIZZLE_ONE;
    }

    FIXME("Invalid component mapping %#x.\n", mapping);
    return VK_COMPONENT_SWIZZLE_IDENTITY;
}

static void vk_component_mapping_from_d3d12(VkComponentMapping *components,
        unsigned int component_mapping)
{
    components->r = vk_component_swizzle_from_d3d12(component_mapping, 0);
    components->g = vk_component_swizzle_from_d3d12(component_mapping, 1);
    components->b = vk_component_swizzle_from_d3d12(component_mapping, 2);
    components->a = vk_component_swizzle_from_d3d12(component_mapping, 3);
}

static VkComponentSwizzle swizzle_vk_component(const VkComponentMapping *components,
        VkComponentSwizzle component, VkComponentSwizzle swizzle)
{
    switch (swizzle)
    {
        case VK_COMPONENT_SWIZZLE_IDENTITY:
            break;

        case VK_COMPONENT_SWIZZLE_R:
            component = components->r;
            break;

        case VK_COMPONENT_SWIZZLE_G:
            component = components->g;
            break;

        case VK_COMPONENT_SWIZZLE_B:
            component = components->b;
            break;

        case VK_COMPONENT_SWIZZLE_A:
            component = components->a;
            break;

        case VK_COMPONENT_SWIZZLE_ONE:
        case VK_COMPONENT_SWIZZLE_ZERO:
            component = swizzle;
            break;

        default:
            FIXME("Invalid component swizzle %#x.\n", swizzle);
            break;
    }

    assert(component != VK_COMPONENT_SWIZZLE_IDENTITY);
    return component;
}

static void vk_component_mapping_compose(VkComponentMapping *dst, const VkComponentMapping *b)
{
    const VkComponentMapping a = *dst;

    dst->r = swizzle_vk_component(&a, a.r, b->r);
    dst->g = swizzle_vk_component(&a, a.g, b->g);
    dst->b = swizzle_vk_component(&a, a.b, b->b);
    dst->a = swizzle_vk_component(&a, a.a, b->a);
}

static bool init_default_texture_view_desc(struct vkd3d_texture_view_desc *desc,
        struct d3d12_resource *resource, DXGI_FORMAT view_format)
{
    const struct d3d12_device *device = resource->device;

    if (!(desc->format = vkd3d_format_from_d3d12_resource_desc(device, &resource->desc, view_format)))
    {
        FIXME("Failed to find format (resource format %#x, view format %#x).\n",
                resource->desc.Format, view_format);
        return false;
    }

    desc->image = resource->vk_image;
    desc->layout = resource->common_layout;
    desc->miplevel_idx = 0;
    desc->miplevel_count = 1;
    desc->layer_idx = 0;
    desc->layer_count = d3d12_resource_desc_get_layer_count(&resource->desc);

    switch (resource->desc.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            desc->view_type = resource->desc.DepthOrArraySize > 1
                    ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            desc->view_type = resource->desc.DepthOrArraySize > 1
                    ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            desc->view_type = VK_IMAGE_VIEW_TYPE_3D;
            desc->layer_count = 1;
            break;

        default:
            FIXME("Resource dimension %#x not implemented.\n", resource->desc.Dimension);
            return false;
    }

    desc->components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->allowed_swizzle = false;
    return true;
}

bool vkd3d_create_texture_view(struct d3d12_device *device, const struct vkd3d_texture_view_desc *desc, struct vkd3d_view **view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct vkd3d_format *format = desc->format;
    struct VkImageViewCreateInfo view_desc;
    struct vkd3d_view *object;
    VkImageView vk_view;
    VkResult vr;

    view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_desc.pNext = NULL;
    view_desc.flags = 0;
    view_desc.image = desc->image;
    view_desc.viewType = desc->view_type;
    view_desc.format = format->vk_format;
    vkd3d_set_view_swizzle_for_format(&view_desc.components, format, desc->allowed_swizzle);
    if (desc->allowed_swizzle)
        vk_component_mapping_compose(&view_desc.components, &desc->components);
    view_desc.subresourceRange.aspectMask = format->vk_aspect_mask;
    view_desc.subresourceRange.baseMipLevel = desc->miplevel_idx;
    view_desc.subresourceRange.levelCount = desc->miplevel_count;
    view_desc.subresourceRange.baseArrayLayer = desc->layer_idx;
    view_desc.subresourceRange.layerCount = desc->layer_count;
    if ((vr = VK_CALL(vkCreateImageView(device->vk_device, &view_desc, NULL, &vk_view))) < 0)
    {
        WARN("Failed to create Vulkan image view, vr %d.\n", vr);
        return false;
    }

    if (!(object = vkd3d_view_create(VKD3D_VIEW_TYPE_IMAGE)))
    {
        VK_CALL(vkDestroyImageView(device->vk_device, vk_view, NULL));
        return false;
    }

    object->vk_image_view = vk_view;
    object->format = format;
    object->info.texture.vk_view_type = desc->view_type;
    object->info.texture.vk_layout = desc->layout;
    object->info.texture.miplevel_idx = desc->miplevel_idx;
    object->info.texture.layer_idx = desc->layer_idx;
    object->info.texture.layer_count = desc->layer_count;
    *view = object;
    return true;
}

static inline void vkd3d_init_write_descriptor_set(VkWriteDescriptorSet *vk_write, const struct d3d12_desc *descriptor,
        VkDescriptorType vk_descriptor_type, const union vkd3d_descriptor_info *info)
{
    vk_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_write->pNext = NULL;
    vk_write->dstSet = descriptor->heap->vk_descriptor_sets[descriptor->metadata.set_index];
    vk_write->dstBinding = 0;
    vk_write->dstArrayElement = d3d12_desc_heap_offset(descriptor);
    vk_write->descriptorCount = 1;
    vk_write->descriptorType = vk_descriptor_type;
    vk_write->pImageInfo = &info->image;
    vk_write->pBufferInfo = &info->buffer;
    vk_write->pTexelBufferView = &info->buffer_view;
}

void d3d12_desc_create_cbv(struct d3d12_desc *descriptor,
        struct d3d12_device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info descriptor_info;
    struct d3d12_resource *resource = NULL;
    VkDescriptorType vk_descriptor_type;
    VkWriteDescriptorSet vk_write;

    if (!desc)
    {
        WARN("Constant buffer desc is NULL.\n");
        return;
    }

    if (desc->SizeInBytes & (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1))
    {
        WARN("Size is not %u bytes aligned.\n", D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        return;
    }

    if (desc->BufferLocation)
    {
        resource = vkd3d_gpu_va_allocator_dereference(&device->gpu_va_allocator, desc->BufferLocation);
        descriptor_info.buffer.buffer = resource->vk_buffer;
        descriptor_info.buffer.offset = desc->BufferLocation - resource->gpu_address;
        descriptor_info.buffer.range = min(desc->SizeInBytes, resource->desc.Width - descriptor_info.buffer.offset);
    }
    else if (device->device_info.robustness2_features.nullDescriptor)
    {
        descriptor_info.buffer.buffer = VK_NULL_HANDLE;
        descriptor_info.buffer.offset = 0;
        descriptor_info.buffer.range = 0;
    }
    else
    {
        descriptor_info.buffer.buffer = device->null_resources.vk_buffer;
        descriptor_info.buffer.offset = 0;
        descriptor_info.buffer.range = VKD3D_NULL_BUFFER_SIZE;
    }

    vk_descriptor_type = vkd3d_bindless_state_get_cbv_descriptor_type(&device->bindless_state);

    descriptor->metadata.cookie = resource ? resource->cookie : 0;
    descriptor->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state, VKD3D_BINDLESS_SET_CBV);
    descriptor->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED;
    descriptor->info.buffer = descriptor_info.buffer;

    vkd3d_init_write_descriptor_set(&vk_write, descriptor, vk_descriptor_type, &descriptor_info);
    VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &vk_write, 0, NULL));
}

static unsigned int vkd3d_view_flags_from_d3d12_buffer_srv_flags(D3D12_BUFFER_SRV_FLAGS flags)
{
    if (flags == D3D12_BUFFER_SRV_FLAG_RAW)
        return VKD3D_VIEW_RAW_BUFFER;
    if (flags)
        FIXME("Unhandled buffer SRV flags %#x.\n", flags);
    return 0;
}

static bool vkd3d_buffer_srv_use_raw_ssbo(struct d3d12_device *device,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    bool raw = !!(desc->Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW);
    return d3d12_device_use_ssbo_raw_buffer(device) &&
            ((desc->Format == DXGI_FORMAT_UNKNOWN && desc->Buffer.StructureByteStride) || raw);
}

static void vkd3d_create_buffer_srv(struct d3d12_desc *descriptor,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    struct vkd3d_null_resources *null_resources = &device->null_resources;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info descriptor_info;
    VkDescriptorType vk_descriptor_type;
    struct vkd3d_view *view = NULL;
    VkWriteDescriptorSet vk_write;
    struct vkd3d_view_key key;

    if (!desc)
    {
        FIXME("Default buffer SRV not supported.\n");
        return;
    }

    if (desc->ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
    {
        WARN("Unexpected view dimension %#x.\n", desc->ViewDimension);
        return;
    }

    if (vkd3d_buffer_srv_use_raw_ssbo(device, desc))
    {
        if (resource)
        {
            uint32_t stride = desc->Format == DXGI_FORMAT_UNKNOWN
                    ? desc->Buffer.StructureByteStride : sizeof(uint32_t);
            descriptor_info.buffer.buffer = resource->vk_buffer;
            descriptor_info.buffer.offset = desc->Buffer.FirstElement * stride + resource->heap_offset;
            descriptor_info.buffer.range = desc->Buffer.NumElements * stride;

            if (descriptor_info.buffer.offset & (d3d12_device_get_ssbo_alignment(device) - 1))
            {
                FIXME("Emitting SSBO at offset #%"PRIx64", but needs alignment of %"PRIu64" bytes.\n",
                      descriptor_info.buffer.offset, d3d12_device_get_ssbo_alignment(device));
            }
        }
        else
        {
            descriptor_info.buffer.buffer = VK_NULL_HANDLE;
            descriptor_info.buffer.offset = 0;
            descriptor_info.buffer.range = 0;
        }

        descriptor->info.buffer = descriptor_info.buffer;
        descriptor->metadata.cookie = resource ? resource->cookie : 0;
        descriptor->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_RAW_SSBO);
        descriptor->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    else
    {
        if (resource)
        {
            unsigned int flags = vkd3d_view_flags_from_d3d12_buffer_srv_flags(desc->Buffer.Flags);

            if (!vkd3d_create_buffer_view_for_resource(device, resource, desc->Format,
                    desc->Buffer.FirstElement, desc->Buffer.NumElements,
                    desc->Buffer.StructureByteStride, flags, &view))
                return;
        }
        else if (!device->device_info.robustness2_features.nullDescriptor)
        {
            key.view_type = VKD3D_VIEW_TYPE_BUFFER;
            key.u.buffer.buffer = null_resources->vk_buffer;
            key.u.buffer.format = vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);
            key.u.buffer.offset = 0;
            key.u.buffer.size = VKD3D_NULL_BUFFER_SIZE;

            if (!(view = vkd3d_view_map_create_view(&device->null_resources.view_map, device, &key)))
                return;
        }

        descriptor_info.buffer_view = view ? view->vk_buffer_view : VK_NULL_HANDLE;

        descriptor->info.view = view;
        descriptor->metadata.cookie = view ? view->cookie : 0;
        descriptor->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_BUFFER);
        descriptor->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED | VKD3D_DESCRIPTOR_FLAG_VIEW;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    }

    vkd3d_init_write_descriptor_set(&vk_write, descriptor, vk_descriptor_type, &descriptor_info);
    VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &vk_write, 0, NULL));
}

static void vkd3d_create_texture_srv(struct d3d12_desc *descriptor,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    struct vkd3d_null_resources *null_resources = &device->null_resources;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info descriptor_info;
    struct vkd3d_view *view = NULL;
    VkWriteDescriptorSet vk_write;
    struct vkd3d_view_key key;

    if (resource)
    {
        if (!init_default_texture_view_desc(&key.u.texture, resource, desc ? desc->Format : 0))
            return;

        key.view_type = VKD3D_VIEW_TYPE_IMAGE;
        key.u.texture.miplevel_count = VK_REMAINING_MIP_LEVELS;
        key.u.texture.allowed_swizzle = true;

        if (desc)
        {
            if (desc->Shader4ComponentMapping != D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING)
            {
                TRACE("Component mapping %s for format %#x.\n",
                        debug_d3d12_shader_component_mapping(desc->Shader4ComponentMapping), desc->Format);

                vk_component_mapping_from_d3d12(&key.u.texture.components, desc->Shader4ComponentMapping);
            }

            switch (desc->ViewDimension)
            {
                case D3D12_SRV_DIMENSION_TEXTURE1D:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D;
                    key.u.texture.miplevel_idx = desc->Texture1D.MostDetailedMip;
                    key.u.texture.miplevel_count = desc->Texture1D.MipLevels;
                    key.u.texture.layer_count = 1;
                    break;
                case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                    key.u.texture.miplevel_idx = desc->Texture1DArray.MostDetailedMip;
                    key.u.texture.miplevel_count = desc->Texture1DArray.MipLevels;
                    key.u.texture.layer_idx = desc->Texture1DArray.FirstArraySlice;
                    key.u.texture.layer_count = desc->Texture1DArray.ArraySize;
                    break;
                case D3D12_SRV_DIMENSION_TEXTURE2D:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                    key.u.texture.miplevel_idx = desc->Texture2D.MostDetailedMip;
                    key.u.texture.miplevel_count = desc->Texture2D.MipLevels;
                    key.u.texture.layer_count = 1;
                    if (desc->Texture2D.PlaneSlice)
                        FIXME("Ignoring plane slice %u.\n", desc->Texture2D.PlaneSlice);
                    if (desc->Texture2D.ResourceMinLODClamp)
                        FIXME("Unhandled min LOD clamp %.8e.\n", desc->Texture2D.ResourceMinLODClamp);
                    break;
                case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                    key.u.texture.miplevel_idx = desc->Texture2DArray.MostDetailedMip;
                    key.u.texture.miplevel_count = desc->Texture2DArray.MipLevels;
                    key.u.texture.layer_idx = desc->Texture2DArray.FirstArraySlice;
                    key.u.texture.layer_count = desc->Texture2DArray.ArraySize;
                    if (desc->Texture2DArray.PlaneSlice)
                        FIXME("Ignoring plane slice %u.\n", desc->Texture2DArray.PlaneSlice);
                    if (desc->Texture2DArray.ResourceMinLODClamp)
                        FIXME("Unhandled min LOD clamp %.8e.\n", desc->Texture2DArray.ResourceMinLODClamp);
                    break;
                case D3D12_SRV_DIMENSION_TEXTURE2DMS:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                    key.u.texture.layer_count = 1;
                    break;
                case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                    key.u.texture.layer_idx = desc->Texture2DMSArray.FirstArraySlice;
                    key.u.texture.layer_count = desc->Texture2DMSArray.ArraySize;
                    break;
                case D3D12_SRV_DIMENSION_TEXTURE3D:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_3D;
                    key.u.texture.miplevel_idx = desc->Texture3D.MostDetailedMip;
                    key.u.texture.miplevel_count = desc->Texture3D.MipLevels;
                    if (desc->Texture3D.ResourceMinLODClamp)
                        FIXME("Unhandled min LOD clamp %.8e.\n", desc->Texture2D.ResourceMinLODClamp);
                    break;
                case D3D12_SRV_DIMENSION_TEXTURECUBE:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_CUBE;
                    key.u.texture.miplevel_idx = desc->TextureCube.MostDetailedMip;
                    key.u.texture.miplevel_count = desc->TextureCube.MipLevels;
                    key.u.texture.layer_count = 6;
                    if (desc->TextureCube.ResourceMinLODClamp)
                        FIXME("Unhandled min LOD clamp %.8e.\n", desc->TextureCube.ResourceMinLODClamp);
                    break;
                case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
                    key.u.texture.miplevel_idx = desc->TextureCubeArray.MostDetailedMip;
                    key.u.texture.miplevel_count = desc->TextureCubeArray.MipLevels;
                    key.u.texture.layer_idx = desc->TextureCubeArray.First2DArrayFace;
                    key.u.texture.layer_count = desc->TextureCubeArray.NumCubes;
                    if (key.u.texture.layer_count != VK_REMAINING_ARRAY_LAYERS)
                        key.u.texture.layer_count *= 6;
                    if (desc->TextureCubeArray.ResourceMinLODClamp)
                        FIXME("Unhandled min LOD clamp %.8e.\n", desc->TextureCubeArray.ResourceMinLODClamp);
                    break;
                default:
                    FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
            }
        }

        if (!(view = vkd3d_view_map_create_view(&resource->view_map, device, &key)))
            return;
    }
    else if (!device->device_info.robustness2_features.nullDescriptor)
    {
        switch (desc->ViewDimension)
        {
            case D3D12_SRV_DIMENSION_TEXTURE2D:
                key.u.texture.image = null_resources->vk_2d_image;
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                key.u.texture.image = null_resources->vk_2d_image;
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                break;

            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
                return;
        }

        key.view_type = VKD3D_VIEW_TYPE_IMAGE;
        key.u.texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        key.u.texture.format = vkd3d_get_format(device, VKD3D_NULL_SRV_FORMAT, false);
        key.u.texture.miplevel_idx = 0;
        key.u.texture.miplevel_count = 1;
        key.u.texture.layer_idx = 0;
        key.u.texture.layer_count = 1;
        key.u.texture.components.r = VK_COMPONENT_SWIZZLE_ZERO;
        key.u.texture.components.g = VK_COMPONENT_SWIZZLE_ZERO;
        key.u.texture.components.b = VK_COMPONENT_SWIZZLE_ZERO;
        key.u.texture.components.a = VK_COMPONENT_SWIZZLE_ZERO;
        key.u.texture.allowed_swizzle = true;

        if (!(view = vkd3d_view_map_create_view(&device->null_resources.view_map, device, &key)))
            return;
    }

    descriptor_info.image.sampler = VK_NULL_HANDLE;
    descriptor_info.image.imageView = view ? view->vk_image_view : VK_NULL_HANDLE;
    descriptor_info.image.imageLayout = view ? view->info.texture.vk_layout : VK_IMAGE_LAYOUT_UNDEFINED;

    descriptor->info.view = view;
    descriptor->metadata.cookie = view ? view->cookie : 0;
    descriptor->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
            VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_IMAGE);
    descriptor->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED | VKD3D_DESCRIPTOR_FLAG_VIEW;

    vkd3d_init_write_descriptor_set(&vk_write, descriptor, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &descriptor_info);
    VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &vk_write, 0, NULL));
}

void d3d12_desc_create_srv(struct d3d12_desc *descriptor,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    bool is_buffer;

    if (resource)
    {
        is_buffer = d3d12_resource_is_buffer(resource);
    }
    else if (desc)
    {
        is_buffer = desc->ViewDimension == D3D12_SRV_DIMENSION_BUFFER;
    }
    else
    {
        WARN("Description required for NULL SRV.");
        return;
    }

    if (is_buffer)
        vkd3d_create_buffer_srv(descriptor, device, resource, desc);
    else
        vkd3d_create_texture_srv(descriptor, device, resource, desc);
}

static unsigned int vkd3d_view_flags_from_d3d12_buffer_uav_flags(D3D12_BUFFER_UAV_FLAGS flags)
{
    if (flags == D3D12_BUFFER_UAV_FLAG_RAW)
        return VKD3D_VIEW_RAW_BUFFER;
    if (flags)
        FIXME("Unhandled buffer UAV flags %#x.\n", flags);
    return 0;
}

VkDeviceAddress vkd3d_get_buffer_device_address(struct d3d12_device *device, VkBuffer vk_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VkBufferDeviceAddressInfoKHR address_info;
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
    address_info.pNext = NULL;
    address_info.buffer = vk_buffer;

    return VK_CALL(vkGetBufferDeviceAddressKHR(device->vk_device, &address_info));
}

static bool vkd3d_buffer_uav_use_raw_ssbo(struct d3d12_device *device,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    bool raw = !!(desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW);
    return d3d12_device_use_ssbo_raw_buffer(device) &&
           ((desc->Format == DXGI_FORMAT_UNKNOWN && desc->Buffer.StructureByteStride) || raw);
}

static void vkd3d_create_buffer_uav(struct d3d12_desc *descriptor, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    struct vkd3d_null_resources *null_resources = &device->null_resources;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info descriptor_info[2];
    unsigned int flags, vk_write_count = 0;
    VkDescriptorType vk_descriptor_type;
    VkDeviceAddress uav_counter_address;
    VkWriteDescriptorSet vk_write[2];
    struct vkd3d_view *view = NULL;
    VkBufferView uav_counter_view;
    struct vkd3d_view_key key;

    if (!desc)
    {
        FIXME("Default buffer UAV not supported.\n");
        return;
    }

    if (desc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
    {
        WARN("Unexpected view dimension %#x.\n", desc->ViewDimension);
        return;
    }

    /* Handle UAV itself */
    flags = vkd3d_view_flags_from_d3d12_buffer_uav_flags(desc->Buffer.Flags);

    if (vkd3d_buffer_uav_use_raw_ssbo(device, desc))
    {
        VkDescriptorBufferInfo *buffer_info = &descriptor_info[vk_write_count].buffer;

        if (resource)
        {
            uint32_t stride = desc->Format == DXGI_FORMAT_UNKNOWN
                    ? desc->Buffer.StructureByteStride : sizeof(uint32_t);
            buffer_info->buffer = resource->vk_buffer;
            buffer_info->offset = desc->Buffer.FirstElement * stride + resource->heap_offset;
            buffer_info->range = desc->Buffer.NumElements * stride;

            if (buffer_info->offset & (d3d12_device_get_ssbo_alignment(device) - 1))
            {
                FIXME("Emitting SSBO at offset #%"PRIx64", but needs alignment of %"PRIu64" bytes.\n",
                      buffer_info->offset, d3d12_device_get_ssbo_alignment(device));
            }
        }
        else
        {
            buffer_info->buffer = VK_NULL_HANDLE;
            buffer_info->offset = 0;
            buffer_info->range = 0;
        }

        descriptor->info.buffer = *buffer_info;
        descriptor->metadata.cookie = resource ? resource->cookie : 0;
        descriptor->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_RAW_SSBO);
        descriptor->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    else
    {
        if (resource)
        {
            if (!vkd3d_create_buffer_view_for_resource(device, resource, desc->Format,
                    desc->Buffer.FirstElement, desc->Buffer.NumElements,
                    desc->Buffer.StructureByteStride, flags, &view))
                return;
        }
        else if (!device->device_info.robustness2_features.nullDescriptor)
        {
            key.view_type = VKD3D_VIEW_TYPE_BUFFER;
            key.u.buffer.buffer = null_resources->vk_storage_buffer;
            key.u.buffer.format = vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);
            key.u.buffer.offset = 0;
            key.u.buffer.size = VKD3D_NULL_BUFFER_SIZE;

            if (!(view = vkd3d_view_map_create_view(&device->null_resources.view_map, device, &key)))
                return;
        }

        descriptor->info.view = view;
        descriptor->metadata.cookie = view ? view->cookie : 0;
        descriptor->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_BUFFER);
        descriptor->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED | VKD3D_DESCRIPTOR_FLAG_VIEW | VKD3D_DESCRIPTOR_FLAG_UAV_COUNTER;

        descriptor_info[vk_write_count].buffer_view = view ? view->vk_buffer_view : VK_NULL_HANDLE;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    }

    vkd3d_init_write_descriptor_set(&vk_write[vk_write_count], descriptor,
            vk_descriptor_type, &descriptor_info[vk_write_count]);
    vk_write_count++;

    /* Handle UAV counter */
    uav_counter_view = VK_NULL_HANDLE;
    uav_counter_address = 0;

    if (resource && counter_resource)
    {
        assert(d3d12_resource_is_buffer(counter_resource));
        assert(desc->Buffer.StructureByteStride);

        if (device->bindless_state.flags & VKD3D_RAW_VA_UAV_COUNTER)
        {
            VkDeviceAddress address = vkd3d_get_buffer_device_address(device, counter_resource->vk_buffer);
            uav_counter_address = address + counter_resource->heap_offset + desc->Buffer.CounterOffsetInBytes;
        }
        else
        {
            struct vkd3d_view *view;

            if (!vkd3d_create_buffer_view_for_resource(device, counter_resource, DXGI_FORMAT_R32_UINT,
                    desc->Buffer.CounterOffsetInBytes / sizeof(uint32_t), 1, 0, 0, &view))
                return;

            uav_counter_view = view->vk_buffer_view;
        }
    }
    else if (!device->device_info.robustness2_features.nullDescriptor)
        uav_counter_view = device->null_resources.vk_storage_buffer_view;

    if (device->bindless_state.flags & VKD3D_RAW_VA_UAV_COUNTER)
    {
        uint32_t descriptor_index = d3d12_desc_heap_offset(descriptor);
        descriptor->heap->uav_counters.data[descriptor_index] = uav_counter_address;
        descriptor->counter_address = uav_counter_address;
    }
    else
    {
        uint32_t set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
            VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_COUNTER);

        descriptor_info[vk_write_count].buffer_view = uav_counter_view;
        vkd3d_init_write_descriptor_set(&vk_write[vk_write_count], descriptor,
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, &descriptor_info[vk_write_count]);

        vk_write[vk_write_count].dstSet = descriptor->heap->vk_descriptor_sets[set_index];
        vk_write_count++;
    }

    VK_CALL(vkUpdateDescriptorSets(device->vk_device, vk_write_count, vk_write, 0, NULL));
}

static void vkd3d_create_texture_uav(struct d3d12_desc *descriptor,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    struct vkd3d_null_resources *null_resources = &device->null_resources;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info descriptor_info;
    struct vkd3d_view *view = NULL;
    VkWriteDescriptorSet vk_write;
    struct vkd3d_view_key key;

    key.view_type = VKD3D_VIEW_TYPE_IMAGE;

    if (resource)
    {
        if (!init_default_texture_view_desc(&key.u.texture, resource, desc ? desc->Format : 0))
            return;

        if (vkd3d_format_is_compressed(key.u.texture.format))
        {
            WARN("UAVs cannot be created for compressed formats.\n");
            return;
        }

        if (desc)
        {
            switch (desc->ViewDimension)
            {
                case D3D12_UAV_DIMENSION_TEXTURE1D:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D;
                    key.u.texture.miplevel_idx = desc->Texture1D.MipSlice;
                    key.u.texture.layer_count = 1;
                    break;
                case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                    key.u.texture.miplevel_idx = desc->Texture1DArray.MipSlice;
                    key.u.texture.layer_idx = desc->Texture1DArray.FirstArraySlice;
                    key.u.texture.layer_count = desc->Texture1DArray.ArraySize;
                    break;
                case D3D12_UAV_DIMENSION_TEXTURE2D:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                    key.u.texture.miplevel_idx = desc->Texture2D.MipSlice;
                    key.u.texture.layer_count = 1;
                    if (desc->Texture2D.PlaneSlice)
                        FIXME("Ignoring plane slice %u.\n", desc->Texture2D.PlaneSlice);
                    break;
                case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                    key.u.texture.miplevel_idx = desc->Texture2DArray.MipSlice;
                    key.u.texture.layer_idx = desc->Texture2DArray.FirstArraySlice;
                    key.u.texture.layer_count = desc->Texture2DArray.ArraySize;
                    if (desc->Texture2DArray.PlaneSlice)
                        FIXME("Ignoring plane slice %u.\n", desc->Texture2DArray.PlaneSlice);
                    break;
                case D3D12_UAV_DIMENSION_TEXTURE3D:
                    key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_3D;
                    key.u.texture.miplevel_idx = desc->Texture3D.MipSlice;
                    if (desc->Texture3D.FirstWSlice ||
                        ((desc->Texture3D.WSize != resource->desc.DepthOrArraySize) && (desc->Texture3D.WSize != UINT_MAX)))
                    {
                        FIXME("Unhandled depth view %u-%u.\n",
                              desc->Texture3D.FirstWSlice, desc->Texture3D.WSize);
                    }
                    break;
                default:
                    FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
            }
        }

        if (!(view = vkd3d_view_map_create_view(&resource->view_map, device, &key)))
            return;
    }
    else if (!device->device_info.robustness2_features.nullDescriptor)
    {
        switch (desc->ViewDimension)
        {
            case D3D12_UAV_DIMENSION_TEXTURE2D:
                key.u.texture.image = null_resources->vk_2d_storage_image;
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                break;
            case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
                key.u.texture.image = null_resources->vk_2d_storage_image;
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                break;

            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
                return;
        }

        key.u.texture.layout = VK_IMAGE_LAYOUT_GENERAL;
        key.u.texture.format = vkd3d_get_format(device, VKD3D_NULL_UAV_FORMAT, false);
        key.u.texture.miplevel_idx = 0;
        key.u.texture.miplevel_count = 1;
        key.u.texture.layer_idx = 0;
        key.u.texture.layer_count = 1;
        key.u.texture.components.r = VK_COMPONENT_SWIZZLE_R;
        key.u.texture.components.g = VK_COMPONENT_SWIZZLE_G;
        key.u.texture.components.b = VK_COMPONENT_SWIZZLE_B;
        key.u.texture.components.a = VK_COMPONENT_SWIZZLE_A;
        key.u.texture.allowed_swizzle = false;

        if (!(view = vkd3d_view_map_create_view(&device->null_resources.view_map, device, &key)))
            return;
    }

    descriptor_info.image.sampler = VK_NULL_HANDLE;
    descriptor_info.image.imageView = view ? view->vk_image_view : VK_NULL_HANDLE;
    descriptor_info.image.imageLayout = view ? view->info.texture.vk_layout : VK_IMAGE_LAYOUT_UNDEFINED;

    descriptor->info.view = view;
    descriptor->metadata.cookie = view ? view->cookie : 0;
    descriptor->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state,
            VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_IMAGE);
    descriptor->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED | VKD3D_DESCRIPTOR_FLAG_VIEW;

    vkd3d_init_write_descriptor_set(&vk_write, descriptor, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &descriptor_info);
    VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &vk_write, 0, NULL));
}

void d3d12_desc_create_uav(struct d3d12_desc *descriptor, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    bool is_buffer;

    if (resource)
    {
        is_buffer = d3d12_resource_is_buffer(resource);
    }
    else if (desc)
    {
        is_buffer = desc->ViewDimension == D3D12_UAV_DIMENSION_BUFFER;
    }
    else
    {
        WARN("Description required for NULL UAV.");
        return;
    }

    if (counter_resource && (!resource || !is_buffer))
        FIXME("Ignoring counter resource %p.\n", counter_resource);

    if (is_buffer)
        vkd3d_create_buffer_uav(descriptor, device, resource, counter_resource, desc);
    else
        vkd3d_create_texture_uav(descriptor, device, resource, desc);
}

bool vkd3d_create_raw_buffer_view(struct d3d12_device *device,
        D3D12_GPU_VIRTUAL_ADDRESS gpu_address, VkBufferView *vk_buffer_view)
{
    const struct vkd3d_format *format;
    struct d3d12_resource *resource;
    uint64_t range;
    uint64_t offset;

    format = vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);
    resource = vkd3d_gpu_va_allocator_dereference(&device->gpu_va_allocator, gpu_address);
    assert(d3d12_resource_is_buffer(resource));

    offset = gpu_address - resource->gpu_address;
    range = min(resource->desc.Width - offset, device->vk_info.device_limits.maxStorageBufferRange);

    return vkd3d_create_vk_buffer_view(device, resource->vk_buffer, format,
            offset, range, vk_buffer_view);
}

/* samplers */
static VkFilter vk_filter_from_d3d12(D3D12_FILTER_TYPE type)
{
    switch (type)
    {
        case D3D12_FILTER_TYPE_POINT:
            return VK_FILTER_NEAREST;
        case D3D12_FILTER_TYPE_LINEAR:
            return VK_FILTER_LINEAR;
        default:
            FIXME("Unhandled filter type %#x.\n", type);
            return VK_FILTER_NEAREST;
    }
}

static VkSamplerMipmapMode vk_mipmap_mode_from_d3d12(D3D12_FILTER_TYPE type)
{
    switch (type)
    {
        case D3D12_FILTER_TYPE_POINT:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case D3D12_FILTER_TYPE_LINEAR:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default:
            FIXME("Unhandled filter type %#x.\n", type);
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
}

static VkSamplerAddressMode vk_address_mode_from_d3d12(D3D12_TEXTURE_ADDRESS_MODE mode)
{
    switch (mode)
    {
        case D3D12_TEXTURE_ADDRESS_MODE_WRAP:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case D3D12_TEXTURE_ADDRESS_MODE_MIRROR:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case D3D12_TEXTURE_ADDRESS_MODE_CLAMP:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case D3D12_TEXTURE_ADDRESS_MODE_BORDER:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            /* D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE requires VK_KHR_mirror_clamp_to_edge. */
        default:
            FIXME("Unhandled address mode %#x.\n", mode);
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkSamplerReductionModeEXT vk_reduction_mode_from_d3d12(D3D12_FILTER_REDUCTION_TYPE mode)
{
    switch (mode)
    {
        case D3D12_FILTER_REDUCTION_TYPE_STANDARD:
        case D3D12_FILTER_REDUCTION_TYPE_COMPARISON:
            return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT;
        case D3D12_FILTER_REDUCTION_TYPE_MINIMUM:
            return VK_SAMPLER_REDUCTION_MODE_MIN_EXT;
        case D3D12_FILTER_REDUCTION_TYPE_MAXIMUM:
            return VK_SAMPLER_REDUCTION_MODE_MAX_EXT;
        default:
            FIXME("Unhandled reduction mode %#x.\n", mode);
            return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT;
    }
}

static bool d3d12_sampler_needs_border_color(D3D12_TEXTURE_ADDRESS_MODE u,
        D3D12_TEXTURE_ADDRESS_MODE v, D3D12_TEXTURE_ADDRESS_MODE w)
{
    return u == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        v == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        w == D3D12_TEXTURE_ADDRESS_MODE_BORDER;
}

static VkBorderColor vk_static_border_color_from_d3d12(D3D12_STATIC_BORDER_COLOR border_color)
{
    switch (border_color)
    {
        case D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        default:
            WARN("Unhandled static border color %u.\n", border_color);
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

static VkBorderColor vk_border_color_from_d3d12(struct d3d12_device *device, const float *border_color)
{
    unsigned int i;

    static const struct
    {
        float color[4];
        VkBorderColor vk_border_color;
    }
    border_colors[] = {
      { {0.0f, 0.0f, 0.0f, 0.0f}, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK },
      { {0.0f, 0.0f, 0.0f, 1.0f}, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK },
      { {1.0f, 1.0f, 1.0f, 1.0f}, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE },
    };

    for (i = 0; i < ARRAY_SIZE(border_colors); i++)
    {
        if (!memcmp(border_color, border_colors[i].color, sizeof(border_colors[i].color)))
            return border_colors[i].vk_border_color;
    }

    if (!device->device_info.custom_border_color_features.customBorderColorWithoutFormat)
    {
        FIXME("Unsupported border color (%f, %f, %f, %f).\n",
                border_color[0], border_color[1], border_color[2], border_color[3]);
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }

    return VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
}

HRESULT d3d12_create_static_sampler(struct d3d12_device *device,
        const D3D12_STATIC_SAMPLER_DESC *desc, VkSampler *vk_sampler)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSamplerReductionModeCreateInfoEXT reduction_desc;
    VkSamplerCreateInfo sampler_desc;
    VkResult vr;

    reduction_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT;
    reduction_desc.pNext = NULL;
    reduction_desc.reductionMode = vk_reduction_mode_from_d3d12(D3D12_DECODE_FILTER_REDUCTION(desc->Filter));

    sampler_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_desc.pNext = NULL;
    sampler_desc.flags = 0;
    sampler_desc.magFilter = vk_filter_from_d3d12(D3D12_DECODE_MAG_FILTER(desc->Filter));
    sampler_desc.minFilter = vk_filter_from_d3d12(D3D12_DECODE_MIN_FILTER(desc->Filter));
    sampler_desc.mipmapMode = vk_mipmap_mode_from_d3d12(D3D12_DECODE_MIP_FILTER(desc->Filter));
    sampler_desc.addressModeU = vk_address_mode_from_d3d12(desc->AddressU);
    sampler_desc.addressModeV = vk_address_mode_from_d3d12(desc->AddressV);
    sampler_desc.addressModeW = vk_address_mode_from_d3d12(desc->AddressW);
    sampler_desc.mipLodBias = desc->MipLODBias;
    sampler_desc.anisotropyEnable = D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc->Filter);
    sampler_desc.maxAnisotropy = desc->MaxAnisotropy;
    sampler_desc.compareEnable = D3D12_DECODE_IS_COMPARISON_FILTER(desc->Filter);
    sampler_desc.compareOp = sampler_desc.compareEnable ? vk_compare_op_from_d3d12(desc->ComparisonFunc) : 0;
    sampler_desc.minLod = desc->MinLOD;
    sampler_desc.maxLod = desc->MaxLOD;
    sampler_desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sampler_desc.unnormalizedCoordinates = VK_FALSE;

    if (d3d12_sampler_needs_border_color(desc->AddressU, desc->AddressV, desc->AddressW))
        sampler_desc.borderColor = vk_static_border_color_from_d3d12(desc->BorderColor);

    if (reduction_desc.reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT && device->vk_info.EXT_sampler_filter_minmax)
        vk_prepend_struct(&sampler_desc, &reduction_desc);

    if ((vr = VK_CALL(vkCreateSampler(device->vk_device, &sampler_desc, NULL, vk_sampler))) < 0)
        WARN("Failed to create Vulkan sampler, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

static HRESULT d3d12_create_sampler(struct d3d12_device *device,
        const D3D12_SAMPLER_DESC *desc, VkSampler *vk_sampler)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSamplerCustomBorderColorCreateInfoEXT border_color_info;
    VkSamplerReductionModeCreateInfoEXT reduction_desc;
    VkSamplerCreateInfo sampler_desc;
    VkResult vr;

    border_color_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
    border_color_info.pNext = NULL;
    memcpy(border_color_info.customBorderColor.float32, desc->BorderColor,
            sizeof(border_color_info.customBorderColor.float32));
    border_color_info.format = VK_FORMAT_UNDEFINED;

    reduction_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT;
    reduction_desc.pNext = NULL;
    reduction_desc.reductionMode = vk_reduction_mode_from_d3d12(D3D12_DECODE_FILTER_REDUCTION(desc->Filter));

    sampler_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_desc.pNext = NULL;
    sampler_desc.flags = 0;
    sampler_desc.magFilter = vk_filter_from_d3d12(D3D12_DECODE_MAG_FILTER(desc->Filter));
    sampler_desc.minFilter = vk_filter_from_d3d12(D3D12_DECODE_MIN_FILTER(desc->Filter));
    sampler_desc.mipmapMode = vk_mipmap_mode_from_d3d12(D3D12_DECODE_MIP_FILTER(desc->Filter));
    sampler_desc.addressModeU = vk_address_mode_from_d3d12(desc->AddressU);
    sampler_desc.addressModeV = vk_address_mode_from_d3d12(desc->AddressV);
    sampler_desc.addressModeW = vk_address_mode_from_d3d12(desc->AddressW);
    sampler_desc.mipLodBias = desc->MipLODBias;
    sampler_desc.anisotropyEnable = D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc->Filter);
    sampler_desc.maxAnisotropy = desc->MaxAnisotropy;
    sampler_desc.compareEnable = D3D12_DECODE_IS_COMPARISON_FILTER(desc->Filter);
    sampler_desc.compareOp = sampler_desc.compareEnable ? vk_compare_op_from_d3d12(desc->ComparisonFunc) : 0;
    sampler_desc.minLod = desc->MinLOD;
    sampler_desc.maxLod = desc->MaxLOD;
    sampler_desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sampler_desc.unnormalizedCoordinates = VK_FALSE;

    if (d3d12_sampler_needs_border_color(desc->AddressU, desc->AddressV, desc->AddressW))
        sampler_desc.borderColor = vk_border_color_from_d3d12(device, desc->BorderColor);

    if (sampler_desc.borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT)
        vk_prepend_struct(&sampler_desc, &border_color_info);

    if (reduction_desc.reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT && device->vk_info.EXT_sampler_filter_minmax)
        vk_prepend_struct(&sampler_desc, &reduction_desc);

    if ((vr = VK_CALL(vkCreateSampler(device->vk_device, &sampler_desc, NULL, vk_sampler))) < 0)
        WARN("Failed to create Vulkan sampler, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

void d3d12_desc_create_sampler(struct d3d12_desc *sampler,
        struct d3d12_device *device, const D3D12_SAMPLER_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info descriptor_info;
    VkWriteDescriptorSet vk_write;
    struct vkd3d_view_key key;
    struct vkd3d_view *view;

    if (!desc)
    {
        WARN("NULL sampler desc.\n");
        return;
    }

    key.view_type = VKD3D_VIEW_TYPE_SAMPLER;
    key.u.sampler = *desc;

    if (!(view = vkd3d_view_map_create_view(&device->sampler_map, device, &key)))
        return;

    sampler->info.view = view;
    sampler->metadata.cookie = view->cookie;
    sampler->metadata.set_index = vkd3d_bindless_state_find_set(&device->bindless_state, VKD3D_BINDLESS_SET_SAMPLER);
    sampler->metadata.flags = VKD3D_DESCRIPTOR_FLAG_DEFINED | VKD3D_DESCRIPTOR_FLAG_VIEW;

    descriptor_info.image.sampler = view->vk_sampler;
    descriptor_info.image.imageView = VK_NULL_HANDLE;
    descriptor_info.image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkd3d_init_write_descriptor_set(&vk_write, sampler, VK_DESCRIPTOR_TYPE_SAMPLER, &descriptor_info);
    VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &vk_write, 0, NULL));
}

/* RTVs */
static void d3d12_rtv_desc_destroy(struct d3d12_rtv_desc *rtv, struct d3d12_device *device)
{
    if (rtv->magic != VKD3D_DESCRIPTOR_MAGIC_RTV)
        return;

    vkd3d_view_decref(rtv->view, device);
    memset(rtv, 0, sizeof(*rtv));
}

void d3d12_rtv_desc_create_rtv(struct d3d12_rtv_desc *rtv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc)
{
    struct vkd3d_texture_view_desc vkd3d_desc;
    struct vkd3d_view *view;

    d3d12_rtv_desc_destroy(rtv_desc, device);

    if (!resource)
    {
        FIXME("NULL resource RTV not implemented.\n");
        return;
    }

    if (!init_default_texture_view_desc(&vkd3d_desc, resource, desc ? desc->Format : 0))
        return;

    if (vkd3d_desc.format->vk_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        WARN("Trying to create RTV for depth/stencil format %#x.\n", vkd3d_desc.format->dxgi_format);
        return;
    }

    vkd3d_desc.layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    if (desc)
    {
        switch (desc->ViewDimension)
        {
            case D3D12_RTV_DIMENSION_TEXTURE1D:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_1D;
                vkd3d_desc.miplevel_idx = desc->Texture1D.MipSlice;
                vkd3d_desc.layer_count = 1;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                vkd3d_desc.miplevel_idx = desc->Texture1DArray.MipSlice;
                vkd3d_desc.layer_idx = desc->Texture1DArray.FirstArraySlice;
                vkd3d_desc.layer_count = desc->Texture1DArray.ArraySize;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2D:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D;
                vkd3d_desc.miplevel_idx = desc->Texture2D.MipSlice;
                vkd3d_desc.layer_count = 1;
                if (desc->Texture2D.PlaneSlice)
                    FIXME("Ignoring plane slice %u.\n", desc->Texture2D.PlaneSlice);
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                vkd3d_desc.miplevel_idx = desc->Texture2DArray.MipSlice;
                vkd3d_desc.layer_idx = desc->Texture2DArray.FirstArraySlice;
                vkd3d_desc.layer_count = desc->Texture2DArray.ArraySize;
                if (desc->Texture2DArray.PlaneSlice)
                    FIXME("Ignoring plane slice %u.\n", desc->Texture2DArray.PlaneSlice);
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DMS:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D;
                vkd3d_desc.layer_count = 1;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                vkd3d_desc.layer_idx = desc->Texture2DMSArray.FirstArraySlice;
                vkd3d_desc.layer_count = desc->Texture2DMSArray.ArraySize;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE3D:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                vkd3d_desc.miplevel_idx = desc->Texture3D.MipSlice;
                vkd3d_desc.layer_idx = desc->Texture3D.FirstWSlice;
                vkd3d_desc.layer_count = desc->Texture3D.WSize;
                break;
            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
        }

        /* Avoid passing down UINT32_MAX here since that makes framebuffer logic later rather awkward. */
        vkd3d_desc.layer_count = min(vkd3d_desc.layer_count, resource->desc.DepthOrArraySize - vkd3d_desc.layer_idx);
    }
    else if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vkd3d_desc.layer_idx = 0;
        vkd3d_desc.layer_count = resource->desc.DepthOrArraySize;
    }

    assert(d3d12_resource_is_texture(resource));

    if (!vkd3d_create_texture_view(device, &vkd3d_desc, &view))
        return;

    rtv_desc->magic = VKD3D_DESCRIPTOR_MAGIC_RTV;
    rtv_desc->sample_count = vk_samples_from_dxgi_sample_desc(&resource->desc.SampleDesc);
    rtv_desc->format = vkd3d_desc.format;
    rtv_desc->width = d3d12_resource_desc_get_width(&resource->desc, vkd3d_desc.miplevel_idx);
    rtv_desc->height = d3d12_resource_desc_get_height(&resource->desc, vkd3d_desc.miplevel_idx);
    rtv_desc->layer_count = vkd3d_desc.layer_count;
    rtv_desc->view = view;
    rtv_desc->resource = resource;
}

/* DSVs */
static void d3d12_dsv_desc_destroy(struct d3d12_dsv_desc *dsv, struct d3d12_device *device)
{
    if (dsv->magic != VKD3D_DESCRIPTOR_MAGIC_DSV)
        return;

    vkd3d_view_decref(dsv->view, device);
    memset(dsv, 0, sizeof(*dsv));
}

static VkImageLayout d3d12_dsv_layout_from_flags(UINT flags)
{
    const D3D12_DSV_FLAGS mask = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;

    switch (flags & mask)
    {
        default: /* case 0: */
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case D3D12_DSV_FLAG_READ_ONLY_DEPTH:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        case D3D12_DSV_FLAG_READ_ONLY_STENCIL:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
        case D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
}

void d3d12_dsv_desc_create_dsv(struct d3d12_dsv_desc *dsv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc)
{
    struct vkd3d_texture_view_desc vkd3d_desc;
    struct vkd3d_view *view;

    d3d12_dsv_desc_destroy(dsv_desc, device);

    if (!resource)
    {
        FIXME("NULL resource DSV not implemented.\n");
        return;
    }

    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        WARN("Cannot create DSV for 3D texture.\n");
        return;
    }

    if (!init_default_texture_view_desc(&vkd3d_desc, resource, desc ? desc->Format : 0))
        return;

    if (!(vkd3d_desc.format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)))
    {
        WARN("Trying to create DSV for format %#x.\n", vkd3d_desc.format->dxgi_format);
        return;
    }

    if (desc)
    {
        vkd3d_desc.layout = d3d12_resource_pick_layout(resource,
                d3d12_dsv_layout_from_flags(desc->Flags));

        switch (desc->ViewDimension)
        {
            case D3D12_DSV_DIMENSION_TEXTURE1D:
                vkd3d_desc.miplevel_idx = desc->Texture1D.MipSlice;
                vkd3d_desc.layer_count = 1;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                vkd3d_desc.miplevel_idx = desc->Texture1DArray.MipSlice;
                vkd3d_desc.layer_idx = desc->Texture1DArray.FirstArraySlice;
                vkd3d_desc.layer_count = desc->Texture1DArray.ArraySize;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2D:
                vkd3d_desc.miplevel_idx = desc->Texture2D.MipSlice;
                vkd3d_desc.layer_count = 1;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                vkd3d_desc.miplevel_idx = desc->Texture2DArray.MipSlice;
                vkd3d_desc.layer_idx = desc->Texture2DArray.FirstArraySlice;
                vkd3d_desc.layer_count = desc->Texture2DArray.ArraySize;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DMS:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D;
                vkd3d_desc.layer_count = 1;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
                vkd3d_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                vkd3d_desc.layer_idx = desc->Texture2DMSArray.FirstArraySlice;
                vkd3d_desc.layer_count = desc->Texture2DMSArray.ArraySize;
                break;
            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
        }

        /* Avoid passing down UINT32_MAX here since that makes framebuffer logic later rather awkward. */
        vkd3d_desc.layer_count = min(vkd3d_desc.layer_count, resource->desc.DepthOrArraySize - vkd3d_desc.layer_idx);
    }
    else
        vkd3d_desc.layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    assert(d3d12_resource_is_texture(resource));

    if (!vkd3d_create_texture_view(device, &vkd3d_desc, &view))
        return;

    dsv_desc->magic = VKD3D_DESCRIPTOR_MAGIC_DSV;
    dsv_desc->sample_count = vk_samples_from_dxgi_sample_desc(&resource->desc.SampleDesc);
    dsv_desc->format = vkd3d_desc.format;
    dsv_desc->width = d3d12_resource_desc_get_width(&resource->desc, vkd3d_desc.miplevel_idx);
    dsv_desc->height = d3d12_resource_desc_get_height(&resource->desc, vkd3d_desc.miplevel_idx);
    dsv_desc->layer_count = vkd3d_desc.layer_count;
    dsv_desc->view = view;
    dsv_desc->resource = resource;
}

/* ID3D12DescriptorHeap */
static inline struct d3d12_descriptor_heap *impl_from_ID3D12DescriptorHeap(ID3D12DescriptorHeap *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_descriptor_heap, ID3D12DescriptorHeap_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_QueryInterface(ID3D12DescriptorHeap *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12DescriptorHeap)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12DescriptorHeap_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_descriptor_heap_AddRef(ID3D12DescriptorHeap *iface)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);
    ULONG refcount = InterlockedIncrement(&heap->refcount);

    TRACE("%p increasing refcount to %u.\n", heap, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_descriptor_heap_Release(ID3D12DescriptorHeap *iface)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);
    ULONG refcount = InterlockedDecrement(&heap->refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = heap->device;
        unsigned int i;

        d3d12_descriptor_heap_cleanup(heap);
        vkd3d_private_store_destroy(&heap->private_store);

        switch (heap->desc.Type)
        {
            case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
                break;

            case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            {
                struct d3d12_rtv_desc *rtvs = (struct d3d12_rtv_desc *)heap->descriptors;

                for (i = 0; i < heap->desc.NumDescriptors; ++i)
                    d3d12_rtv_desc_destroy(&rtvs[i], device);
                break;
            }

            case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            {
                struct d3d12_dsv_desc *dsvs = (struct d3d12_dsv_desc *)heap->descriptors;

                for (i = 0; i < heap->desc.NumDescriptors; ++i)
                    d3d12_dsv_desc_destroy(&dsvs[i], device);
                break;
            }

            default:
                break;
        }

        vkd3d_free(heap);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_GetPrivateData(ID3D12DescriptorHeap *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_SetPrivateData(ID3D12DescriptorHeap *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_SetPrivateDataInterface(ID3D12DescriptorHeap *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&heap->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_SetName(ID3D12DescriptorHeap *iface, const WCHAR *name)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, heap->device->wchar_size));

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_GetDevice(ID3D12DescriptorHeap *iface, REFIID iid, void **device)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(heap->device, iid, device);
}

static D3D12_DESCRIPTOR_HEAP_DESC * STDMETHODCALLTYPE d3d12_descriptor_heap_GetDesc(ID3D12DescriptorHeap *iface,
        D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = heap->desc;
    return desc;
}

static D3D12_CPU_DESCRIPTOR_HANDLE * STDMETHODCALLTYPE d3d12_descriptor_heap_GetCPUDescriptorHandleForHeapStart(
        ID3D12DescriptorHeap *iface, D3D12_CPU_DESCRIPTOR_HANDLE *descriptor)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, descriptor %p.\n", iface, descriptor);

    descriptor->ptr = (SIZE_T)heap->descriptors;

    return descriptor;
}

static D3D12_GPU_DESCRIPTOR_HANDLE * STDMETHODCALLTYPE d3d12_descriptor_heap_GetGPUDescriptorHandleForHeapStart(
        ID3D12DescriptorHeap *iface, D3D12_GPU_DESCRIPTOR_HANDLE *descriptor)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, descriptor %p.\n", iface, descriptor);

    descriptor->ptr = (uint64_t)(intptr_t)heap->descriptors;

    return descriptor;
}

static CONST_VTBL struct ID3D12DescriptorHeapVtbl d3d12_descriptor_heap_vtbl =
{
    /* IUnknown methods */
    d3d12_descriptor_heap_QueryInterface,
    d3d12_descriptor_heap_AddRef,
    d3d12_descriptor_heap_Release,
    /* ID3D12Object methods */
    d3d12_descriptor_heap_GetPrivateData,
    d3d12_descriptor_heap_SetPrivateData,
    d3d12_descriptor_heap_SetPrivateDataInterface,
    d3d12_descriptor_heap_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_descriptor_heap_GetDevice,
    /* ID3D12DescriptorHeap methods */
    d3d12_descriptor_heap_GetDesc,
    d3d12_descriptor_heap_GetCPUDescriptorHandleForHeapStart,
    d3d12_descriptor_heap_GetGPUDescriptorHandleForHeapStart,
};

struct d3d12_descriptor_heap *unsafe_impl_from_ID3D12DescriptorHeap(ID3D12DescriptorHeap *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_descriptor_heap_vtbl);
    return impl_from_ID3D12DescriptorHeap(iface);
}

static HRESULT d3d12_descriptor_heap_create_descriptor_pool(struct d3d12_descriptor_heap *descriptor_heap,
        VkDescriptorPool *vk_descriptor_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    VkDescriptorPoolSize vk_pool_sizes[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    const struct d3d12_device *device = descriptor_heap->device;
    VkDescriptorPoolCreateInfo vk_pool_info;
    unsigned int i, pool_count = 0;
    VkResult vr;

    for (i = 0; i < device->bindless_state.set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &device->bindless_state.set_info[i];

        if (set_info->heap_type == descriptor_heap->desc.Type)
        {
            VkDescriptorPoolSize *vk_pool_size = &vk_pool_sizes[pool_count++];
            vk_pool_size->type = set_info->vk_descriptor_type;
            vk_pool_size->descriptorCount = descriptor_heap->desc.NumDescriptors;
        }
    }

    if (!pool_count)
        return S_OK;

    vk_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    vk_pool_info.pNext = NULL;
    vk_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
    vk_pool_info.maxSets = pool_count;
    vk_pool_info.poolSizeCount = pool_count;
    vk_pool_info.pPoolSizes = vk_pool_sizes;

    if ((vr = VK_CALL(vkCreateDescriptorPool(device->vk_device,
            &vk_pool_info, NULL, vk_descriptor_pool))) < 0)
    {
        ERR("Failed to create descriptor pool, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static void d3d12_descriptor_heap_zero_initialize(struct d3d12_descriptor_heap *descriptor_heap,
        VkDescriptorType vk_descriptor_type, VkDescriptorSet vk_descriptor_set, uint32_t descriptor_count)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    const struct d3d12_device *device = descriptor_heap->device;
    VkDescriptorBufferInfo *buffer_infos = NULL;
    VkDescriptorImageInfo *image_infos = NULL;
    VkBufferView *buffer_view_infos = NULL;
    VkWriteDescriptorSet write;
    uint32_t i;

    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = NULL;
    write.descriptorType = vk_descriptor_type;
    write.dstSet = vk_descriptor_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = descriptor_count;
    write.pTexelBufferView = NULL;
    write.pImageInfo = NULL;
    write.pBufferInfo = NULL;

    switch (vk_descriptor_type)
    {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        image_infos = vkd3d_calloc(descriptor_count, sizeof(*image_infos));
        write.pImageInfo = image_infos;
        break;

    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        buffer_infos = vkd3d_calloc(descriptor_count, sizeof(*buffer_infos));
        write.pBufferInfo = buffer_infos;
        for (i = 0; i < descriptor_count; i++)
            buffer_infos[i].range = VK_WHOLE_SIZE;
        break;

    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        buffer_view_infos = vkd3d_calloc(descriptor_count, sizeof(*buffer_view_infos));
        write.pTexelBufferView = buffer_view_infos;
        break;

    default:
        break;
    }

    VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &write, 0, NULL));
    vkd3d_free(image_infos);
    vkd3d_free(buffer_view_infos);
    vkd3d_free(buffer_infos);
}

static HRESULT d3d12_descriptor_heap_create_descriptor_set(struct d3d12_descriptor_heap *descriptor_heap,
        const struct vkd3d_bindless_set_info *binding, VkDescriptorSet *vk_descriptor_set)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    VkDescriptorSetVariableDescriptorCountAllocateInfoEXT vk_variable_count_info;
    uint32_t descriptor_count = descriptor_heap->desc.NumDescriptors;
    const struct d3d12_device *device = descriptor_heap->device;
    VkDescriptorSetAllocateInfo vk_set_info;
    VkResult vr;

    vk_variable_count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
    vk_variable_count_info.pNext = NULL;
    vk_variable_count_info.descriptorSetCount = 1;
    vk_variable_count_info.pDescriptorCounts = &descriptor_count;

    vk_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    vk_set_info.pNext = &vk_variable_count_info;
    vk_set_info.descriptorPool = descriptor_heap->vk_descriptor_pool;
    vk_set_info.descriptorSetCount = 1;
    vk_set_info.pSetLayouts = &binding->vk_host_set_layout;

    if (descriptor_heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        vk_set_info.pSetLayouts = &binding->vk_set_layout;

    if ((vr = VK_CALL(vkAllocateDescriptorSets(device->vk_device, &vk_set_info, vk_descriptor_set))) < 0)
    {
        ERR("Failed to allocate descriptor set, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (device->device_info.robustness2_features.nullDescriptor &&
        binding->vk_descriptor_type != VK_DESCRIPTOR_TYPE_SAMPLER)
    {
        d3d12_descriptor_heap_zero_initialize(descriptor_heap,
                binding->vk_descriptor_type, *vk_descriptor_set, descriptor_count);
    }

    return S_OK;
}

static HRESULT d3d12_descriptor_heap_create_uav_counter_buffer(struct d3d12_descriptor_heap *descriptor_heap,
        struct d3d12_descriptor_heap_uav_counters *uav_counters)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    struct d3d12_device *device = descriptor_heap->device;
    D3D12_HEAP_PROPERTIES heap_info;
    D3D12_RESOURCE_DESC buffer_desc;
    D3D12_HEAP_FLAGS heap_flags;
    VkResult vr;
    HRESULT hr;

    /* concurrently accessible storage buffer */
    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = descriptor_heap->desc.NumDescriptors * sizeof(VkDeviceAddress);
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    /* host-visible device memory */
    memset(&heap_info, 0, sizeof(heap_info));
    heap_info.Type = D3D12_HEAP_TYPE_UPLOAD;

    heap_flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    if (FAILED(hr = vkd3d_create_buffer(device, &heap_info, heap_flags, &buffer_desc, &uav_counters->vk_buffer)))
        return hr;

    if (FAILED(hr = vkd3d_allocate_buffer_memory(device, uav_counters->vk_buffer, NULL,
            &heap_info, heap_flags, &uav_counters->vk_memory, NULL, NULL)))
        return hr;

    if ((vr = VK_CALL(vkMapMemory(device->vk_device, uav_counters->vk_memory,
            0, VK_WHOLE_SIZE, 0, (void **)&uav_counters->data))))
    {
        ERR("Failed to map UAV counter address buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT d3d12_descriptor_heap_init(struct d3d12_descriptor_heap *descriptor_heap,
        struct d3d12_device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    unsigned int i, set_index = 0;
    HRESULT hr;

    memset(descriptor_heap, 0, sizeof(*descriptor_heap));
    descriptor_heap->ID3D12DescriptorHeap_iface.lpVtbl = &d3d12_descriptor_heap_vtbl;
    descriptor_heap->refcount = 1;
    descriptor_heap->device = device;
    descriptor_heap->desc = *desc;

    if (FAILED(hr = d3d12_descriptor_heap_create_descriptor_pool(descriptor_heap,
            &descriptor_heap->vk_descriptor_pool)))
        goto fail;

    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        for (i = 0; i < device->bindless_state.set_count; i++)
        {
            const struct vkd3d_bindless_set_info *set_info = &device->bindless_state.set_info[i];

            if (set_info->heap_type == desc->Type)
            {
                if (FAILED(hr = d3d12_descriptor_heap_create_descriptor_set(descriptor_heap,
                        set_info, &descriptor_heap->vk_descriptor_sets[set_index++])))
                    goto fail;
            }
        }
    }

    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
            (device->bindless_state.flags & VKD3D_RAW_VA_UAV_COUNTER))
    {
        if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        {
            if (FAILED(hr = d3d12_descriptor_heap_create_uav_counter_buffer(descriptor_heap,
                    &descriptor_heap->uav_counters)))
                goto fail;
        }
        else if (!(descriptor_heap->uav_counters.data = vkd3d_calloc(desc->NumDescriptors, sizeof(VkDeviceSize))))
        {
            ERR("Failed to allocate UAV counter address buffer.\n");
            goto fail;
        }
    }

    if (FAILED(hr = vkd3d_private_store_init(&descriptor_heap->private_store)))
        goto fail;

    d3d12_device_add_ref(descriptor_heap->device);
    return S_OK;

fail:
    d3d12_descriptor_heap_cleanup(descriptor_heap);
    return hr;
}

static void d3d12_descriptor_heap_init_descriptors(struct d3d12_descriptor_heap *descriptor_heap,
        size_t descriptor_size)
{
    struct d3d12_desc *desc;
    unsigned int i;

    memset(descriptor_heap->descriptors, 0, descriptor_size * descriptor_heap->desc.NumDescriptors);

    switch (descriptor_heap->desc.Type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            desc = (struct d3d12_desc *)descriptor_heap->descriptors;

            for (i = 0; i < descriptor_heap->desc.NumDescriptors; i++)
            {
                desc[i].heap = descriptor_heap;
                desc[i].heap_offset = i;
            }
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            break;

        default:
            WARN("Unhandled descriptor heap type: %d.\n", descriptor_heap->desc.Type);
    }
}

HRESULT d3d12_descriptor_heap_create(struct d3d12_device *device,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, struct d3d12_descriptor_heap **descriptor_heap)
{
    size_t max_descriptor_count, descriptor_size;
    struct d3d12_descriptor_heap *object;
    HRESULT hr;

    if (!(descriptor_size = d3d12_device_get_descriptor_handle_increment_size(device, desc->Type)))
    {
        WARN("No descriptor size for descriptor type %#x.\n", desc->Type);
        return E_INVALIDARG;
    }

    if ((desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
            && (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV))
    {
        WARN("RTV/DSV descriptor heaps cannot be shader visible.\n");
        return E_INVALIDARG;
    }

    max_descriptor_count = (~(size_t)0 - sizeof(*object)) / descriptor_size;
    if (desc->NumDescriptors > max_descriptor_count)
    {
        WARN("Invalid descriptor count %u (max %zu).\n", desc->NumDescriptors, max_descriptor_count);
        return E_OUTOFMEMORY;
    }

    if (!(object = vkd3d_malloc(offsetof(struct d3d12_descriptor_heap,
            descriptors[descriptor_size * desc->NumDescriptors]))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_descriptor_heap_init(object, device, desc)))
    {
        vkd3d_free(object);
        return hr;
    }

    d3d12_descriptor_heap_init_descriptors(object, descriptor_size);

    TRACE("Created descriptor heap %p.\n", object);

    *descriptor_heap = object;

    return S_OK;
}

void d3d12_descriptor_heap_cleanup(struct d3d12_descriptor_heap *descriptor_heap)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    const struct d3d12_device *device = descriptor_heap->device;

    if (!descriptor_heap->uav_counters.vk_memory)
        vkd3d_free(descriptor_heap->uav_counters.data);

    VK_CALL(vkDestroyBuffer(device->vk_device, descriptor_heap->uav_counters.vk_buffer, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, descriptor_heap->uav_counters.vk_memory, NULL));

    VK_CALL(vkDestroyDescriptorPool(device->vk_device, descriptor_heap->vk_descriptor_pool, NULL));
}

/* ID3D12QueryHeap */
static inline struct d3d12_query_heap *impl_from_ID3D12QueryHeap(ID3D12QueryHeap *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_query_heap, ID3D12QueryHeap_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_QueryInterface(ID3D12QueryHeap *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID3D12QueryHeap)
            || IsEqualGUID(iid, &IID_ID3D12Pageable)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12QueryHeap_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_query_heap_AddRef(ID3D12QueryHeap *iface)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);
    ULONG refcount = InterlockedIncrement(&heap->refcount);

    TRACE("%p increasing refcount to %u.\n", heap, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_query_heap_Release(ID3D12QueryHeap *iface)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);
    ULONG refcount = InterlockedDecrement(&heap->refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = heap->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        vkd3d_private_store_destroy(&heap->private_store);

        VK_CALL(vkDestroyQueryPool(device->vk_device, heap->vk_query_pool, NULL));

        vkd3d_free(heap);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_GetPrivateData(ID3D12QueryHeap *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_SetPrivateData(ID3D12QueryHeap *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_SetPrivateDataInterface(ID3D12QueryHeap *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&heap->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_SetName(ID3D12QueryHeap *iface, const WCHAR *name)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, heap->device->wchar_size));

    return vkd3d_set_vk_object_name(heap->device, (uint64_t)heap->vk_query_pool,
            VK_OBJECT_TYPE_QUERY_POOL, name);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_GetDevice(ID3D12QueryHeap *iface, REFIID iid, void **device)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(heap->device, iid, device);
}

static CONST_VTBL struct ID3D12QueryHeapVtbl d3d12_query_heap_vtbl =
{
    /* IUnknown methods */
    d3d12_query_heap_QueryInterface,
    d3d12_query_heap_AddRef,
    d3d12_query_heap_Release,
    /* ID3D12Object methods */
    d3d12_query_heap_GetPrivateData,
    d3d12_query_heap_SetPrivateData,
    d3d12_query_heap_SetPrivateDataInterface,
    d3d12_query_heap_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_query_heap_GetDevice,
};

struct d3d12_query_heap *unsafe_impl_from_ID3D12QueryHeap(ID3D12QueryHeap *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_query_heap_vtbl);
    return impl_from_ID3D12QueryHeap(iface);
}

HRESULT d3d12_query_heap_create(struct d3d12_device *device, const D3D12_QUERY_HEAP_DESC *desc,
        struct d3d12_query_heap **heap)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct d3d12_query_heap *object;
    VkQueryPoolCreateInfo pool_info;
    unsigned int element_count;
    VkResult vr;
    HRESULT hr;

    element_count = DIV_ROUND_UP(desc->Count, sizeof(*object->availability_mask) * CHAR_BIT);
    if (!(object = vkd3d_malloc(offsetof(struct d3d12_query_heap, availability_mask[element_count]))))
        return E_OUTOFMEMORY;

    object->ID3D12QueryHeap_iface.lpVtbl = &d3d12_query_heap_vtbl;
    object->refcount = 1;
    object->device = device;
    memset(object->availability_mask, 0, element_count * sizeof(*object->availability_mask));

    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = 0;
    pool_info.queryCount = desc->Count;

    switch (desc->Type)
    {
        case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
            pool_info.queryType = VK_QUERY_TYPE_OCCLUSION;
            pool_info.pipelineStatistics = 0;
            break;

        case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
            pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            pool_info.pipelineStatistics = 0;
            break;

        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
            pool_info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
            pool_info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT
                    | VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
            break;

        case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            if (!device->vk_info.transform_feedback_queries)
            {
                FIXME("Transform feedback queries are not supported by Vulkan implementation.\n");
                vkd3d_free(object);
                return E_NOTIMPL;
            }

            pool_info.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
            pool_info.pipelineStatistics = 0;
            break;

        default:
            WARN("Invalid query heap type %u.\n", desc->Type);
            vkd3d_free(object);
            return E_INVALIDARG;
    }

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    if ((vr = VK_CALL(vkCreateQueryPool(device->vk_device, &pool_info, NULL, &object->vk_query_pool))) < 0)
    {
        WARN("Failed to create Vulkan query pool, vr %d.\n", vr);
        vkd3d_private_store_destroy(&object->private_store);
        vkd3d_free(object);
        return hresult_from_vk_result(vr);
    }

    d3d12_device_add_ref(device);

    TRACE("Created query heap %p.\n", object);

    *heap = object;

    return S_OK;
}

static HRESULT vkd3d_init_null_resources_data(struct vkd3d_null_resources *null_resource,
        struct d3d12_device *device)
{
    const bool use_sparse_resources = device->vk_info.sparse_properties.residencyNonResidentStrict;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    static const VkClearColorValue clear_color = {{0}};
    VkCommandBufferAllocateInfo command_buffer_info;
    VkCommandPool vk_command_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo command_pool_info;
    VkDevice vk_device = device->vk_device;
    VkCommandBufferBeginInfo begin_info;
    VkCommandBuffer vk_command_buffer;
    VkFence vk_fence = VK_NULL_HANDLE;
    VkImageSubresourceRange range;
    VkImageMemoryBarrier barrier;
    VkFenceCreateInfo fence_info;
    struct vkd3d_queue *queue;
    VkSubmitInfo submit_info;
    VkQueue vk_queue;
    VkResult vr;

    queue = d3d12_device_get_vkd3d_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);

    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.pNext = NULL;
    command_pool_info.flags = 0;
    command_pool_info.queueFamilyIndex = queue->vk_family_index;

    if ((vr = VK_CALL(vkCreateCommandPool(vk_device, &command_pool_info, NULL, &vk_command_pool))) < 0)
    {
        WARN("Failed to create Vulkan command pool, vr %d.\n", vr);
        goto done;
    }

    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = NULL;
    command_buffer_info.commandPool = vk_command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(vk_device, &command_buffer_info, &vk_command_buffer))) < 0)
    {
        WARN("Failed to allocate Vulkan command buffer, vr %d.\n", vr);
        goto done;
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = VK_CALL(vkBeginCommandBuffer(vk_command_buffer, &begin_info))) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        goto done;
    }

    /* fill buffer */
    VK_CALL(vkCmdFillBuffer(vk_command_buffer, null_resource->vk_buffer, 0, VK_WHOLE_SIZE, 0x00000000));

    if (use_sparse_resources)
    {
        /* transition 2D UAV image */
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext = NULL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = null_resource->vk_2d_storage_image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        VK_CALL(vkCmdPipelineBarrier(vk_command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                0, NULL, 0, NULL, 1, &barrier));
    }
    else
    {
        /* fill UAV buffer */
        VK_CALL(vkCmdFillBuffer(vk_command_buffer,
                null_resource->vk_storage_buffer, 0, VK_WHOLE_SIZE, 0x00000000));

        /* clear 2D UAV image */
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext = NULL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = null_resource->vk_2d_storage_image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        VK_CALL(vkCmdPipelineBarrier(vk_command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, NULL, 0, NULL, 1, &barrier));

        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        VK_CALL(vkCmdClearColorImage(vk_command_buffer,
                null_resource->vk_2d_storage_image, VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1, &range));
    }

    /* transition 2D SRV image */
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = null_resource->vk_2d_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VK_CALL(vkCmdPipelineBarrier(vk_command_buffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
            0, NULL, 0, NULL, 1, &barrier));

    if ((vr = VK_CALL(vkEndCommandBuffer(vk_command_buffer))) < 0)
    {
        WARN("Failed to end command buffer, vr %d.\n", vr);
        goto done;
    }

    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = NULL;
    fence_info.flags = 0;

    if ((vr = VK_CALL(vkCreateFence(device->vk_device, &fence_info, NULL, &vk_fence))) < 0)
    {
        WARN("Failed to create Vulkan fence, vr %d.\n", vr);
        goto done;
    }

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;
    submit_info.pWaitDstStageMask = NULL;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vk_command_buffer;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = NULL;

    if (!(vk_queue = vkd3d_queue_acquire(queue)))
    {
        WARN("Failed to acquire queue %p.\n", queue);
        goto done;
    }

    if ((vr = VK_CALL(vkQueueSubmit(vk_queue, 1, &submit_info, vk_fence))) < 0)
        ERR("Failed to submit, vr %d.\n", vr);

    vkd3d_queue_release(queue);

    vr = VK_CALL(vkWaitForFences(device->vk_device, 1, &vk_fence, VK_FALSE, ~(uint64_t)0));
    if (vr != VK_SUCCESS)
        WARN("Failed to wait fo fence, vr %d.\n", vr);

done:
    VK_CALL(vkDestroyCommandPool(vk_device, vk_command_pool, NULL));
    VK_CALL(vkDestroyFence(vk_device, vk_fence, NULL));

    return hresult_from_vk_result(vr);
}

HRESULT vkd3d_init_null_resources(struct vkd3d_null_resources *null_resources,
        struct d3d12_device *device)
{
    const bool use_sparse_resources = device->vk_info.sparse_properties.residencyNonResidentStrict;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    HRESULT hr;

    TRACE("Creating resources for NULL views.\n");

    memset(null_resources, 0, sizeof(*null_resources));

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    /* buffer */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = VKD3D_NULL_BUFFER_SIZE;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(hr = vkd3d_create_buffer(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, &null_resources->vk_buffer)))
        goto fail;
    if (FAILED(hr = vkd3d_allocate_buffer_memory(device, null_resources->vk_buffer, NULL,
            &heap_properties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, &null_resources->vk_buffer_memory, NULL, NULL)))
        goto fail;
    if (!vkd3d_create_vk_buffer_view(device, null_resources->vk_buffer,
            vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false),
            0, VK_WHOLE_SIZE, &null_resources->vk_buffer_view))
        goto fail;

    /* buffer UAV */
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(hr = vkd3d_create_buffer(device, use_sparse_resources ? NULL : &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, &null_resources->vk_storage_buffer)))
        goto fail;
    if (!use_sparse_resources && FAILED(hr = vkd3d_allocate_buffer_memory(device, null_resources->vk_storage_buffer, NULL,
            &heap_properties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, &null_resources->vk_storage_buffer_memory, NULL, NULL)))
        goto fail;
    if (!vkd3d_create_vk_buffer_view(device, null_resources->vk_storage_buffer,
            vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false),
            0, VK_WHOLE_SIZE, &null_resources->vk_storage_buffer_view))
        goto fail;

    /* 2D SRV */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 1;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = VKD3D_NULL_SRV_FORMAT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(hr = vkd3d_create_image(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, NULL, &null_resources->vk_2d_image)))
        goto fail;
    if (FAILED(hr = vkd3d_allocate_image_memory(device, null_resources->vk_2d_image,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &null_resources->vk_2d_image_memory, NULL, NULL)))
        goto fail;

    /* 2D UAV */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 1;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = VKD3D_NULL_UAV_FORMAT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = use_sparse_resources
            ? D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE : D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(hr = vkd3d_create_image(device, use_sparse_resources ? NULL : &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, NULL, &null_resources->vk_2d_storage_image)))
        goto fail;
    if (!use_sparse_resources && FAILED(hr = vkd3d_allocate_image_memory(device, null_resources->vk_2d_storage_image,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &null_resources->vk_2d_storage_image_memory, NULL, NULL)))
        goto fail;

    /* set Vulkan object names */
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_buffer,
            VK_OBJECT_TYPE_BUFFER, "NULL buffer");
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_buffer_view,
            VK_OBJECT_TYPE_BUFFER_VIEW, "NULL buffer view");
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_buffer_memory,
            VK_OBJECT_TYPE_DEVICE_MEMORY, "NULL memory");
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_storage_buffer,
            VK_OBJECT_TYPE_BUFFER, "NULL UAV buffer");
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_storage_buffer_view,
            VK_OBJECT_TYPE_BUFFER_VIEW, "NULL UAV buffer view");
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_2d_image,
            VK_OBJECT_TYPE_IMAGE, "NULL 2D SRV image");
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_2d_image_memory,
            VK_OBJECT_TYPE_DEVICE_MEMORY, "NULL 2D SRV memory");
    vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_2d_storage_image,
            VK_OBJECT_TYPE_IMAGE, "NULL 2D UAV image");
    if (!use_sparse_resources)
    {
        vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_storage_buffer_memory,
                VK_OBJECT_TYPE_DEVICE_MEMORY, "NULL UAV buffer memory");
        vkd3d_set_vk_object_name_utf8(device, (uint64_t)null_resources->vk_2d_storage_image_memory,
                VK_OBJECT_TYPE_DEVICE_MEMORY, "NULL 2D UAV memory");
    }

    if (FAILED(hr = vkd3d_view_map_init(&null_resources->view_map)))
        return hr;

    return vkd3d_init_null_resources_data(null_resources, device);

fail:
    ERR("Failed to initialize NULL resources, hr %#x.\n", hr);
    vkd3d_destroy_null_resources(null_resources, device);
    return hr;
}

void vkd3d_destroy_null_resources(struct vkd3d_null_resources *null_resources,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    vkd3d_view_map_destroy(&null_resources->view_map, device);

    VK_CALL(vkDestroyBufferView(device->vk_device, null_resources->vk_buffer_view, NULL));
    VK_CALL(vkDestroyBuffer(device->vk_device, null_resources->vk_buffer, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, null_resources->vk_buffer_memory, NULL));

    VK_CALL(vkDestroyBufferView(device->vk_device, null_resources->vk_storage_buffer_view, NULL));
    VK_CALL(vkDestroyBuffer(device->vk_device, null_resources->vk_storage_buffer, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, null_resources->vk_storage_buffer_memory, NULL));

    VK_CALL(vkDestroyImage(device->vk_device, null_resources->vk_2d_image, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, null_resources->vk_2d_image_memory, NULL));

    VK_CALL(vkDestroyImage(device->vk_device, null_resources->vk_2d_storage_image, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, null_resources->vk_2d_storage_image_memory, NULL));

    memset(null_resources, 0, sizeof(*null_resources));
}

static uint32_t vkd3d_memory_info_find_global_mask(struct d3d12_device *device)
{
    /* Never allow memory types from any PCI-pinned heap.
     * If we allow it, it might end up being used as a fallback memory type, which will cause severe instabilities.
     * These types should only be used in a controlled fashion. */
    VkDeviceSize largest_device_local_heap_size = 0;
    VkDeviceSize largest_host_only_heap_size = 0;
    uint32_t largest_device_local_heap_index = 0;
    uint32_t largest_host_only_heap_index = 0;
    uint32_t device_local_heap_count = 0;
    uint32_t host_only_heap_count = 0;
    bool exists_device_only_type;
    VkMemoryPropertyFlags flags;
    bool exists_host_only_type;
    VkDeviceSize heap_size;
    uint32_t heap_index;
    uint32_t i, mask;

    for (i = 0; i < device->memory_properties.memoryHeapCount; i++)
    {
        heap_size = device->memory_properties.memoryHeaps[i].size;
        if (device->memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            if (heap_size > largest_device_local_heap_size)
            {
                largest_device_local_heap_index = i;
                largest_device_local_heap_size = heap_size;
            }
            device_local_heap_count++;
        }
        else
        {
            if (heap_size > largest_host_only_heap_size)
            {
                largest_host_only_heap_index = i;
                largest_host_only_heap_size = heap_size;
            }
            host_only_heap_count++;
        }
    }

    /* If we only have one device local heap, or no host-only heaps, there is nothing to do. */
    if (device_local_heap_count <= 1 || host_only_heap_count == 0)
        return UINT32_MAX;

    /* Verify that there exists a DEVICE_LOCAL type that is not HOST_VISIBLE on this device
     * which maps to the largest device local heap. That way, it is safe to mask out all memory types which are
     * DEVICE_LOCAL | HOST_VISIBLE.
     * Similarly, there must exist a host-only type. */
    exists_device_only_type = false;
    exists_host_only_type = false;
    for (i = 0; i < device->memory_properties.memoryTypeCount; i++)
    {
        flags = device->memory_properties.memoryTypes[i].propertyFlags;
        heap_index = device->memory_properties.memoryTypes[i].heapIndex;

        if (heap_index == largest_device_local_heap_index &&
            (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            exists_device_only_type = true;
        }
        else if (heap_index == largest_host_only_heap_index &&
                 (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 &&
                 (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
        {
            exists_host_only_type = true;
        }
    }

    if (!exists_device_only_type || !exists_host_only_type)
        return UINT32_MAX;

    /* Mask out any memory types which are deemed problematic. */
    for (i = 0, mask = 0; i < device->memory_properties.memoryTypeCount; i++)
    {
        const VkMemoryPropertyFlags pinned_mask = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        flags = device->memory_properties.memoryTypes[i].propertyFlags;
        heap_index = device->memory_properties.memoryTypes[i].propertyFlags;

        if (heap_index != largest_device_local_heap_index &&
            heap_index != largest_host_only_heap_index &&
            (flags & pinned_mask) == pinned_mask)
        {
            mask |= 1u << i;
            WARN("Blocking memory type %u for use (PCI-pinned memory).\n", i);
        }
    }

    return ~mask;
}

HRESULT vkd3d_memory_info_init(struct vkd3d_memory_info *info,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryRequirements memory_requirements;
    VkBufferCreateInfo buffer_info;
    VkImageCreateInfo image_info;
    VkBuffer buffer;
    VkImage image;
    VkResult vr;

    info->global_mask = vkd3d_memory_info_find_global_mask(device);

    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = 65536;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    if ((vr = VK_CALL(vkCreateBuffer(device->vk_device, &buffer_info, NULL, &buffer))) < 0)
    {
        ERR("Failed to create dummy buffer");
        return hresult_from_vk_result(vr);
    }

    VK_CALL(vkGetBufferMemoryRequirements(device->vk_device, buffer, &memory_requirements));
    VK_CALL(vkDestroyBuffer(device->vk_device, buffer, NULL));
    info->buffer_type_mask = memory_requirements.memoryTypeBits;

    memset(&image_info, 0, sizeof(image_info));
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent.width = 16;
    image_info.extent.height = 16;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if ((vr = VK_CALL(vkCreateImage(device->vk_device, &image_info, NULL, &image))) < 0)
    {
        ERR("Failed to create dummy sampled image");
        return hresult_from_vk_result(vr);
    }

    VK_CALL(vkGetImageMemoryRequirements(device->vk_device, image, &memory_requirements));
    VK_CALL(vkDestroyImage(device->vk_device, image, NULL));
    info->sampled_type_mask = memory_requirements.memoryTypeBits;

    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT;

    if ((vr = VK_CALL(vkCreateImage(device->vk_device, &image_info, NULL, &image))) < 0)
    {
        ERR("Failed to create dummy color image");
        return hresult_from_vk_result(vr);
    }

    VK_CALL(vkGetImageMemoryRequirements(device->vk_device, image, &memory_requirements));
    VK_CALL(vkDestroyImage(device->vk_device, image, NULL));
    info->rt_ds_type_mask = memory_requirements.memoryTypeBits;

    image_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;

    if ((vr = VK_CALL(vkCreateImage(device->vk_device, &image_info, NULL, &image))) < 0)
    {
        ERR("Failed to create dummy depth-stencil image");
        return hresult_from_vk_result(vr);
    }

    VK_CALL(vkGetImageMemoryRequirements(device->vk_device, image, &memory_requirements));
    VK_CALL(vkDestroyImage(device->vk_device, image, NULL));
    info->rt_ds_type_mask &= memory_requirements.memoryTypeBits;

    info->buffer_type_mask &= info->global_mask;
    info->sampled_type_mask &= info->global_mask;
    info->rt_ds_type_mask &= info->global_mask;

    TRACE("Device supports buffers on memory types 0x%#x.\n", info->buffer_type_mask);
    TRACE("Device supports textures on memory types 0x%#x.\n", info->sampled_type_mask);
    TRACE("Device supports render targets on memory types 0x%#x.\n", info->rt_ds_type_mask);
    return S_OK;
}
