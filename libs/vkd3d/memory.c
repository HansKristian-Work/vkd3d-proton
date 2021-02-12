/*
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

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
#include "vkd3d_descriptor_debug.h"
#endif

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

static uint32_t vkd3d_find_memory_types_with_flags(struct d3d12_device *device, VkMemoryPropertyFlags type_flags)
{
    const VkPhysicalDeviceMemoryProperties *memory_info = &device->memory_properties;
    uint32_t i, mask = 0;

    for (i = 0; i < memory_info->memoryTypeCount; i++)
    {
        if ((memory_info->memoryTypes[i].propertyFlags & type_flags) == type_flags)
            mask |= 1u << i;
    }

    return mask;
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

static HRESULT vkd3d_create_global_buffer(struct d3d12_device *device, VkDeviceSize size, const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, VkBuffer *vk_buffer)
{
    D3D12_RESOURCE_DESC resource_desc;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (heap_flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER)
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    if (heap_properties->Type != D3D12_HEAP_TYPE_UPLOAD &&
            heap_properties->Type != D3D12_HEAP_TYPE_READBACK)
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return vkd3d_create_buffer(device, heap_properties, heap_flags, &resource_desc, vk_buffer);
}

static HRESULT vkd3d_try_allocate_device_memory_2(struct d3d12_device *device,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, VkDeviceMemory *vk_memory, uint32_t *vk_memory_type)
{
    const VkPhysicalDeviceMemoryProperties *memory_info = &device->memory_properties;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryAllocateInfo allocate_info;
    VkResult vr;

    /* buffer_mask / sampled_mask etc will generally take care of this,
     * but for certain fallback scenarios where we select other memory
     * types, we need to mask here as well. */
    type_mask &= device->memory_info.global_mask;

    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = pNext;
    allocate_info.allocationSize = size;

    while (type_mask)
    {
        uint32_t type_index = vkd3d_bitmask_iter32(&type_mask);

        if ((memory_info->memoryTypes[type_index].propertyFlags & type_flags) != type_flags)
            continue;

        allocate_info.memoryTypeIndex = type_index;

        if ((vr = VK_CALL(vkAllocateMemory(device->vk_device,
                &allocate_info, NULL, vk_memory))) == VK_SUCCESS)
        {
            if (vk_memory_type)
                *vk_memory_type = type_index;

            return S_OK;
        }
    }

    return E_OUTOFMEMORY;
}

static HRESULT vkd3d_allocate_device_memory_2(struct d3d12_device *device,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, VkDeviceMemory *vk_memory, uint32_t *vk_memory_type)
{
    const VkMemoryPropertyFlags optional_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    HRESULT hr;

    hr = vkd3d_try_allocate_device_memory_2(device, size, type_flags,
            type_mask, pNext, vk_memory, vk_memory_type);

    if (FAILED(hr) && (type_flags & optional_flags))
    {
        WARN("Memory allocation failed, falling back to system memory.\n");
        hr = vkd3d_try_allocate_device_memory_2(device, size,
                type_flags & ~optional_flags, type_mask, pNext,
                vk_memory, vk_memory_type);
    }

    if (FAILED(hr))
    {
        ERR("Failed to allocate device memory (size %"PRIu64", type_flags %#x, type_mask %#x).\n",
                size, type_flags, type_mask);
    }

    return hr;
}

static HRESULT vkd3d_import_host_memory_2(struct d3d12_device *device, void *host_address,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, VkDeviceMemory *vk_memory, uint32_t *vk_memory_type)
{
    VkImportMemoryHostPointerInfoEXT import_info;
    HRESULT hr;

    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
    import_info.pNext = pNext;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    import_info.pHostPointer = host_address;

    if (FAILED(hr = vkd3d_try_allocate_device_memory_2(device, size,
            type_flags, type_mask, &import_info, vk_memory, vk_memory_type)))
    {
        WARN("Failed to import host memory, hr %#x.\n", hr);
        /* If we failed, fall back to a host-visible allocation. Generally
         * the app will access the memory thorugh the main host pointer,
         * so it's fine. */
        hr = vkd3d_try_allocate_device_memory_2(device, size,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                type_mask, &import_info, vk_memory, vk_memory_type);
    }

    return hr;
}

static HRESULT vkd3d_allocation_assign_gpu_address(struct vkd3d_memory_allocation *allocation, struct d3d12_device *device, struct vkd3d_memory_allocator *allocator)
{
    if (device->device_info.buffer_device_address_features.bufferDeviceAddress)
        allocation->resource.va = vkd3d_get_buffer_device_address(device, allocation->resource.vk_buffer);
    else
        allocation->resource.va = vkd3d_va_map_alloc_fake_va(&allocator->va_map, allocation->resource.size);

    if (!allocation->resource.va)
    {
        ERR("Failed to get GPU address for allocation.\n");
        return E_OUTOFMEMORY;
    }

    vkd3d_va_map_insert(&allocator->va_map, &allocation->resource);
    return S_OK;
}

static void vkd3d_memory_allocation_free(const struct vkd3d_memory_allocation *allocation, struct d3d12_device *device, struct vkd3d_memory_allocator *allocator)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("allocation %p, device %p, allocator %p.\n", allocation, device, allocator);

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    vkd3d_descriptor_debug_unregister_cookie(allocation->resource.cookie);
#endif

    if ((allocation->flags & VKD3D_ALLOCATION_FLAG_GPU_ADDRESS) && allocation->resource.va)
    {
        vkd3d_va_map_remove(&allocator->va_map, &allocation->resource);

        if (!device->device_info.buffer_device_address_features.bufferDeviceAddress)
            vkd3d_va_map_free_fake_va(&allocator->va_map, allocation->resource.va, allocation->resource.size);
    }

    if (allocation->flags & VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER)
        VK_CALL(vkDestroyBuffer(device->vk_device, allocation->resource.vk_buffer, NULL));

    VK_CALL(vkFreeMemory(device->vk_device, allocation->vk_memory, NULL));
}

static HRESULT vkd3d_memory_allocation_init(struct vkd3d_memory_allocation *allocation, struct d3d12_device *device,
        struct vkd3d_memory_allocator *allocator, const struct vkd3d_allocate_memory_info *info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryRequirements memory_requirements;
    VkMemoryAllocateFlagsInfo flags_info;
    VkMemoryPropertyFlags type_flags;
    uint32_t type_mask;
    VkResult vr;
    HRESULT hr;

    TRACE("allocation %p, device %p, allocator %p, info %p.\n", allocation, device, allocator, info);

    memset(allocation, 0, sizeof(*allocation));
    allocation->heap_type = info->heap_properties.Type;
    allocation->heap_flags = info->heap_flags;
    allocation->flags = info->flags;

    /* This also sort of validates the heap description,
     * so we want to do this before creating any objects */
    if (FAILED(hr = vkd3d_select_memory_flags(device, &info->heap_properties, &type_flags)))
        return hr;

    if (allocation->flags & VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER)
    {
        /* If requested, create a buffer covering the entire allocation
         * and derive the exact memory requirements from that. Any buffer
         * resources are just going to use this buffer with an offset. */
        if (FAILED(hr = vkd3d_create_global_buffer(device, info->memory_requirements.size,
                &info->heap_properties, info->heap_flags, &allocation->resource.vk_buffer)))
            return hr;

        VK_CALL(vkGetBufferMemoryRequirements(device->vk_device,
                allocation->resource.vk_buffer, &memory_requirements));

        memory_requirements.memoryTypeBits &= info->memory_requirements.memoryTypeBits;
    }
    else
    {
        /* Respect existing memory requirements since there may not
         * be any buffer resource to get memory requirements from. */
        memory_requirements = info->memory_requirements;
    }

    /* For dedicated buffer allocations we should assign the existing
     * buffer for address lookup purposes, but take care not to destroy
     * it when freeing the allocation. */
    if (allocation->flags & VKD3D_ALLOCATION_FLAG_DEDICATED_BUFFER)
        allocation->resource.vk_buffer = info->vk_buffer;

    type_mask = vkd3d_select_memory_types(device, &info->heap_properties,
            info->heap_flags) & memory_requirements.memoryTypeBits;

    /* Allocate actual backing storage */
    flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.pNext = info->pNext;
    flags_info.flags = 0;

    if (allocation->resource.vk_buffer)
    {
        allocation->flags |= VKD3D_ALLOCATION_FLAG_GPU_ADDRESS;

        if (device->device_info.buffer_device_address_features.bufferDeviceAddress)
            flags_info.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    }

    allocation->resource.size = memory_requirements.size;

    if (info->host_ptr)
    {
        hr = vkd3d_import_host_memory_2(device, info->host_ptr, memory_requirements.size,
                type_flags, type_mask, &flags_info, &allocation->vk_memory, &allocation->vk_memory_type);
    }
    else
    {
        hr = vkd3d_allocate_device_memory_2(device, memory_requirements.size, type_flags,
                type_mask, &flags_info, &allocation->vk_memory, &allocation->vk_memory_type);
    }

    if (FAILED(hr))
        return hr;

    /* Map memory if the allocation was requested to be host-visible,
     * but do not map if the allocation was meant to be device-local
     * since that may negatively impact performance. */
    if (type_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocation->flags |= VKD3D_ALLOCATION_FLAG_CPU_ACCESS;

        if ((vr = VK_CALL(vkMapMemory(device->vk_device, allocation->vk_memory,
                0, VK_WHOLE_SIZE, 0, &allocation->cpu_address))))
        {
            ERR("Failed to map memory, vr %d.\n", vr);
            vkd3d_memory_allocation_free(allocation, device, allocator);
            return hresult_from_vk_result(vr);
        }
    }

    /* Bind memory to global or dedicated buffer as needed */
    if (allocation->resource.vk_buffer)
    {
        if ((vr = VK_CALL(vkBindBufferMemory(device->vk_device,
                allocation->resource.vk_buffer, allocation->vk_memory, 0))) < 0)
        {
            ERR("Failed to bind buffer memory, vr %d.\n", vr);
            vkd3d_memory_allocation_free(allocation, device, allocator);
            return hresult_from_vk_result(vr);
        }

        /* Assign GPU address as necessary. */
        if (allocation->flags & VKD3D_ALLOCATION_FLAG_GPU_ADDRESS)
        {
            if (FAILED(hr = vkd3d_allocation_assign_gpu_address(allocation, device, allocator)))
            {
                vkd3d_memory_allocation_free(allocation, device, allocator);
                return hresult_from_vk_result(vr);
            }
        }
    }

    allocation->resource.cookie = vkd3d_allocate_cookie();
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    vkd3d_descriptor_debug_register_allocation_cookie(allocation->resource.cookie, info);
#endif

    TRACE("Created allocation %p on memory type %u (%"PRIu64" bytes).\n",
            allocation, allocation->vk_memory_type, allocation->resource.size);
    return S_OK;
}

static void vkd3d_memory_chunk_insert_range(struct vkd3d_memory_chunk *chunk,
        size_t index, VkDeviceSize offset, VkDeviceSize length)
{
    if (!vkd3d_array_reserve((void**)&chunk->free_ranges, &chunk->free_ranges_size,
            chunk->free_ranges_count + 1, sizeof(*chunk->free_ranges)))
    {
        ERR("Failed to insert free range.\n");
        return;
    }

    memmove(&chunk->free_ranges[index + 1], &chunk->free_ranges[index],
            sizeof(*chunk->free_ranges) * (chunk->free_ranges_count - index));

    chunk->free_ranges[index].offset = offset;
    chunk->free_ranges[index].length = length;
    chunk->free_ranges_count++;
}

static void vkd3d_memory_chunk_remove_range(struct vkd3d_memory_chunk *chunk, size_t index)
{
    chunk->free_ranges_count--;

    memmove(&chunk->free_ranges[index], &chunk->free_ranges[index + 1],
            sizeof(*chunk->free_ranges) * (chunk->free_ranges_count - index));
}

static HRESULT vkd3d_memory_chunk_allocate_range(struct vkd3d_memory_chunk *chunk, const VkMemoryRequirements *memory_requirements,
        struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_memory_free_range *pick_range;
    VkDeviceSize l_length, r_length;
    size_t i, pick_index;

    if (!chunk->free_ranges_count)
        return E_OUTOFMEMORY;

    pick_index = chunk->free_ranges_count;
    pick_range = NULL;

    for (i = 0; i < chunk->free_ranges_count; i++)
    {
        struct vkd3d_memory_free_range *range = &chunk->free_ranges[i];

        if (range->offset + range->length - align(range->offset, memory_requirements->alignment) < memory_requirements->size)
            continue;

        /* Exact fit leaving no gaps */
        if (range->length == memory_requirements->size)
        {
            pick_index = i;
            pick_range = range;
            break;
        }

        /* Alignment is almost always going to be 64 KiB, so
         * don't worry too much about misalignment gaps here */
        if (!pick_range || range->length > pick_range->length)
        {
            pick_index = i;
            pick_range = range;
        }
    }

    if (!pick_range)
        return E_OUTOFMEMORY;

    /* Adjust offsets and addresses of the base allocation */
    vkd3d_memory_allocation_slice(allocation, &chunk->allocation,
            align(pick_range->offset, memory_requirements->alignment),
            memory_requirements->size);

    /* Remove allocated range from the free list */
    l_length = allocation->offset - pick_range->offset;
    r_length = pick_range->offset + pick_range->length
            - allocation->offset - allocation->resource.size;

    if (l_length)
    {
        pick_range->length = l_length;

        if (r_length)
        {
            vkd3d_memory_chunk_insert_range(chunk, pick_index + 1,
                allocation->offset + allocation->resource.size, r_length);
        }
    }
    else if (r_length)
    {
        pick_range->offset = allocation->offset + allocation->resource.size;
        pick_range->length = r_length;
    }
    else
    {
        vkd3d_memory_chunk_remove_range(chunk, pick_index);
    }

    return S_OK;
}

static size_t vkd3d_memory_chunk_find_range(struct vkd3d_memory_chunk *chunk, VkDeviceSize offset)
{
    struct vkd3d_memory_free_range *range;
    size_t index, hi, lo;

    lo = 0;
    hi = chunk->free_ranges_count;

    while (lo < hi)
    {
        index = lo + (hi - lo) / 2;
        range = &chunk->free_ranges[index];

        if (range->offset > offset)
            hi = index;
        else
            lo = index + 1;
    }

    return lo;
}

static void vkd3d_memory_chunk_free_range(struct vkd3d_memory_chunk *chunk, const struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_memory_free_range *range;
    bool adjacent_l, adjacent_r;
    size_t index;

    index = vkd3d_memory_chunk_find_range(chunk, allocation->offset);

    adjacent_l = false;
    adjacent_r = false;

    if (index > 0)
    {
        range = &chunk->free_ranges[index - 1];
        adjacent_l = range->offset + range->length == allocation->offset;
    }

    if (index < chunk->free_ranges_count)
    {
        range = &chunk->free_ranges[index];
        adjacent_r = range->offset == allocation->offset + allocation->resource.size;
    }

    if (adjacent_l)
    {
        range = &chunk->free_ranges[index - 1];
        range->length += allocation->resource.size;

        if (adjacent_r)
        {
            range->length += chunk->free_ranges[index].length;
            vkd3d_memory_chunk_remove_range(chunk, index);
        }
    }
    else if (adjacent_r)
    {
        range = &chunk->free_ranges[index];
        range->offset = allocation->offset;
        range->length += allocation->resource.size;
    }
    else
    {
        vkd3d_memory_chunk_insert_range(chunk, index,
                allocation->offset, allocation->resource.size);
    }
}

static bool vkd3d_memory_chunk_is_free(struct vkd3d_memory_chunk *chunk)
{
    return chunk->free_ranges_count == 1 && chunk->free_ranges[0].length == chunk->allocation.resource.size;
}

static HRESULT vkd3d_memory_chunk_create(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_memory_info *info, struct vkd3d_memory_chunk **chunk)
{
    struct vkd3d_memory_chunk *object;
    HRESULT hr;

    TRACE("device %p, allocator %p, info %p, chunk %p.\n", device, allocator, info, chunk);

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));

    if (FAILED(hr = vkd3d_memory_allocation_init(&object->allocation, device, allocator, info)))
    {
        vkd3d_free(object);
        return hr;
    }

    object->allocation.chunk = object;
    vkd3d_memory_chunk_insert_range(object, 0, 0, object->allocation.resource.size);
    *chunk = object;

    TRACE("Created chunk %p (allocation %p).\n", object, &object->allocation);
    return S_OK;
}

static void vkd3d_memory_chunk_destroy(struct vkd3d_memory_chunk *chunk, struct d3d12_device *device, struct vkd3d_memory_allocator *allocator)
{
    TRACE("chunk %p, device %p, allocator %p.\n", chunk, device, allocator);

    vkd3d_memory_allocation_free(&chunk->allocation, device, allocator);
    vkd3d_free(chunk->free_ranges);
    vkd3d_free(chunk);
}

static void vkd3d_memory_allocator_remove_chunk(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device, struct vkd3d_memory_chunk *chunk)
{
    size_t i;

    for (i = 0; i < allocator->chunks_count; i++)
    {
        if (allocator->chunks[i] == chunk)
        {
            allocator->chunks[i] = allocator->chunks[--allocator->chunks_count];
            break;
        }
    }

    vkd3d_memory_chunk_destroy(chunk, device, allocator);
}

HRESULT vkd3d_memory_allocator_init(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device)
{
    int rc;

    memset(allocator, 0, sizeof(*allocator));

    if ((rc = pthread_mutex_init(&allocator->mutex, NULL)))
        return hresult_from_errno(rc);

    vkd3d_va_map_init(&allocator->va_map);
    return S_OK;
}

void vkd3d_memory_allocator_cleanup(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device)
{
    size_t i;

    for (i = 0; i < allocator->chunks_count; i++)
        vkd3d_memory_chunk_destroy(allocator->chunks[i], device, allocator);

    vkd3d_free(allocator->chunks);
    vkd3d_va_map_cleanup(&allocator->va_map);
    pthread_mutex_destroy(&allocator->mutex);
}

static HRESULT vkd3d_memory_allocator_add_chunk(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, uint32_t type_mask, struct vkd3d_memory_chunk **chunk)
{
    struct vkd3d_allocate_memory_info alloc_info;
    struct vkd3d_memory_chunk *object;
    HRESULT hr;

    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.memory_requirements.size = VKD3D_MEMORY_CHUNK_SIZE;
    alloc_info.memory_requirements.alignment = 0;
    alloc_info.memory_requirements.memoryTypeBits = type_mask;
    alloc_info.heap_properties = *heap_properties;
    alloc_info.heap_flags = heap_flags;

    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_BUFFERS))
        alloc_info.flags |= VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER;

    if (!vkd3d_array_reserve((void**)&allocator->chunks, &allocator->chunks_size,
            allocator->chunks_count + 1, sizeof(*allocator->chunks)))
    {
        ERR("Failed to allocate space for new chunk.\n");
        return E_OUTOFMEMORY;
    }

    if (FAILED(hr = vkd3d_memory_chunk_create(device, allocator, &alloc_info, &object)))
        return hr;

    allocator->chunks[allocator->chunks_count++] = *chunk = object;
    return S_OK;
}

static HRESULT vkd3d_memory_allocator_try_suballocate_memory(struct vkd3d_memory_allocator *allocator,
        struct d3d12_device *device, const VkMemoryRequirements *memory_requirements, uint32_t type_mask,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_memory_chunk *chunk;
    HRESULT hr;
    size_t i;

    type_mask &= device->memory_info.global_mask;
    type_mask &= memory_requirements->memoryTypeBits;

    for (i = 0; i < allocator->chunks_count; i++)
    {
        chunk = allocator->chunks[i];

        /* Match flags since otherwise the backing buffer
         * may not support our required usage flags */
        if (chunk->allocation.heap_type != heap_properties->Type ||
                chunk->allocation.heap_flags != heap_flags)
            continue;

        /* Filter out unsupported memory types */
        if (!(type_mask & (1u << chunk->allocation.vk_memory_type)))
            continue;

        if (SUCCEEDED(hr = vkd3d_memory_chunk_allocate_range(chunk, memory_requirements, allocation)))
            return hr;
    }

    /* Try allocating a new chunk on one of the supported memory type
     * before the caller falls back to potentially slower memory */
    if (FAILED(hr = vkd3d_memory_allocator_add_chunk(allocator, device, heap_properties,
            heap_flags, memory_requirements->memoryTypeBits, &chunk)))
        return hr;

    return vkd3d_memory_chunk_allocate_range(chunk, memory_requirements, allocation);
}

void vkd3d_free_memory_2(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_memory_allocation *allocation)
{
    if (allocation->chunk)
    {
        pthread_mutex_lock(&allocator->mutex);
        vkd3d_memory_chunk_free_range(allocation->chunk, allocation);

        if (vkd3d_memory_chunk_is_free(allocation->chunk))
            vkd3d_memory_allocator_remove_chunk(allocator, device, allocation->chunk);
        pthread_mutex_unlock(&allocator->mutex);
    }
    else
        vkd3d_memory_allocation_free(allocation, device, allocator);
}

static HRESULT vkd3d_suballocate_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_memory_info *info, struct vkd3d_memory_allocation *allocation)
{
    const VkMemoryPropertyFlags optional_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryRequirements memory_requirements = info->memory_requirements;
    uint32_t required_mask, optional_mask;
    VkMemoryPropertyFlags type_flags;
    HRESULT hr;
    
    if (FAILED(hr = vkd3d_select_memory_flags(device, &info->heap_properties, &type_flags)))
        return hr;

    /* Prefer device-local memory if allowed for this allocation */
    required_mask = vkd3d_find_memory_types_with_flags(device, type_flags & ~optional_flags);
    optional_mask = vkd3d_find_memory_types_with_flags(device, type_flags);

    pthread_mutex_lock(&allocator->mutex);

    hr = vkd3d_memory_allocator_try_suballocate_memory(allocator, device,
            &memory_requirements, optional_mask, &info->heap_properties,
            info->heap_flags, allocation);

    if (FAILED(hr) && (required_mask & ~optional_mask))
    {
        hr = vkd3d_memory_allocator_try_suballocate_memory(allocator, device,
                &memory_requirements, required_mask & ~optional_mask,
                &info->heap_properties, info->heap_flags, allocation);
    }

    pthread_mutex_unlock(&allocator->mutex);
    return hr;
}

static HRESULT vkd3d_allocate_memory_2(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_memory_info *info, struct vkd3d_memory_allocation *allocation)
{
    if (!info->pNext && !info->host_ptr && info->memory_requirements.size < VKD3D_VA_BLOCK_SIZE)
        return vkd3d_suballocate_memory(device, allocator, info, allocation);
    else
        return vkd3d_memory_allocation_init(allocation, device, allocator, info);
}

HRESULT vkd3d_allocate_heap_memory_2(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_heap_memory_info *info, struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_allocate_memory_info alloc_info;

    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.memory_requirements.memoryTypeBits = ~0u;
    alloc_info.memory_requirements.alignment = info->heap_desc.Alignment;
    alloc_info.memory_requirements.size = info->heap_desc.SizeInBytes;
    alloc_info.heap_properties = info->heap_desc.Properties;
    alloc_info.heap_flags = info->heap_desc.Flags;
    alloc_info.host_ptr = info->host_ptr;

    if (!(info->heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS))
        alloc_info.flags |= VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER;

    return vkd3d_allocate_memory_2(device, allocator, &alloc_info, allocation);
}

HRESULT vkd3d_allocate_resource_memory_2(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_resource_memory_info *info, struct vkd3d_memory_allocation *allocation)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_allocate_memory_info alloc_info;
    VkMemoryDedicatedAllocateInfo dedicated_info;
    VkResult vr;
    HRESULT hr;

    assert((!info->vk_image) != (!info->vk_buffer));

    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.pNext = NULL;
    dedicated_info.buffer = info->vk_buffer;
    dedicated_info.image = info->vk_image;

    memset(&alloc_info, 0, sizeof(alloc_info));
    if (info->vk_image)
        VK_CALL(vkGetImageMemoryRequirements(device->vk_device, info->vk_image, &alloc_info.memory_requirements));
    else /* if (info->vk_buffer) */
        VK_CALL(vkGetBufferMemoryRequirements(device->vk_device, info->vk_buffer, &alloc_info.memory_requirements));
    alloc_info.heap_properties = info->heap_properties;
    alloc_info.heap_flags = info->heap_flags;
    alloc_info.host_ptr = info->host_ptr;
    alloc_info.vk_buffer = info->vk_buffer;
    alloc_info.pNext = &dedicated_info;

    if (info->vk_buffer)
        alloc_info.flags = VKD3D_ALLOCATION_FLAG_DEDICATED_BUFFER;

    if (FAILED(hr = vkd3d_allocate_memory_2(device, allocator, &alloc_info, allocation)))
        return hr;

    /* Buffer memory binds are handled in vkd3d_allocate_memory,
     * so we only need to handle image memory here */
    if (info->vk_image)
    {
        if ((vr = VK_CALL(vkBindImageMemory(device->vk_device,
                info->vk_image, allocation->vk_memory, allocation->offset))) < 0)
        {
            ERR("Failed to bind image memory, vr %d.\n", vr);
            vkd3d_free_memory_2(device, allocator, allocation);
            return hresult_from_vk_result(vr);
        }
    }

    return S_OK;
}
