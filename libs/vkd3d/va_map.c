/*
 * Copyright 2021 Philip Rebohle for Valve Software
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

static inline VkDeviceAddress vkd3d_va_map_get_next_address(VkDeviceAddress va)
{
    return va >> (VKD3D_VA_BLOCK_SIZE_BITS + VKD3D_VA_BLOCK_BITS);
}

static inline VkDeviceAddress vkd3d_va_map_get_block_address(VkDeviceAddress va)
{
    return (va >> VKD3D_VA_BLOCK_SIZE_BITS) & VKD3D_VA_BLOCK_MASK;
}

static struct vkd3d_va_block *vkd3d_va_map_find_block(struct vkd3d_va_map *va_map, VkDeviceAddress va)
{
    VkDeviceAddress next_address = vkd3d_va_map_get_next_address(va);
    struct vkd3d_va_tree *tree = &va_map->va_tree;

    while (next_address && tree)
    {
        tree = vkd3d_atomic_ptr_load_explicit(&tree->next[next_address & VKD3D_VA_NEXT_MASK], vkd3d_memory_order_acquire);
        next_address >>= VKD3D_VA_NEXT_BITS;
    }
    
    if (!tree)
        return NULL;

    return &tree->blocks[vkd3d_va_map_get_block_address(va)];
}

static struct vkd3d_va_block *vkd3d_va_map_get_block(struct vkd3d_va_map *va_map, VkDeviceAddress va)
{
    VkDeviceAddress next_address = vkd3d_va_map_get_next_address(va);
    struct vkd3d_va_tree *tree, **tree_ptr;
    
    tree = &va_map->va_tree;

    while (next_address)
    {
        tree_ptr = &tree->next[next_address & VKD3D_VA_NEXT_MASK];
        tree = vkd3d_atomic_ptr_load_explicit(tree_ptr, vkd3d_memory_order_acquire);

        if (!tree)
        {
            void *orig;
            tree = vkd3d_calloc(1, sizeof(*tree));
            orig = vkd3d_atomic_ptr_compare_exchange(tree_ptr, NULL, tree, vkd3d_memory_order_release, vkd3d_memory_order_acquire);

            if (orig)
            {
                vkd3d_free(tree);
                tree = orig;
            }
        }

        next_address >>= VKD3D_VA_NEXT_BITS;
    }
    
    return &tree->blocks[vkd3d_va_map_get_block_address(va)];
}

static void vkd3d_va_map_cleanup_tree(struct vkd3d_va_tree *tree)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(tree->next); i++)
    {
        if (tree->next[i])
        {
            vkd3d_va_map_cleanup_tree(tree->next[i]);
            vkd3d_free(tree->next[i]);
        }
    }
}

static struct vkd3d_unique_resource *vkd3d_va_map_find_small_entry(struct vkd3d_va_map *va_map,
        VkDeviceAddress va, size_t *index)
{
    struct vkd3d_unique_resource *resource = NULL;
    size_t hi = va_map->small_entries_count;
    size_t lo = 0;

    while (lo < hi)
    {
        struct vkd3d_unique_resource *r;
        size_t i = lo + (hi - lo) / 2;

        r = va_map->small_entries[i];

        if (va < r->va)
            hi = i;
        else if (va >= r->va + r->size)
            lo = i + 1;
        else
        {
            lo = hi = i;
            resource = r;
        }
    }

    if (index)
        *index = lo;

    return resource;
}

void vkd3d_va_map_insert(struct vkd3d_va_map *va_map, struct vkd3d_unique_resource *resource)
{
    VkDeviceAddress block_va, min_va, max_va;
    struct vkd3d_va_block *block;
    size_t index;

    if (resource->size >= VKD3D_VA_BLOCK_SIZE)
    {
        min_va = resource->va;
        max_va = resource->va + resource->size;
        block_va = min_va & ~VKD3D_VA_LO_MASK;

        while (block_va < max_va)
        {
            block = vkd3d_va_map_get_block(va_map, block_va);

            if (block_va < min_va)
            {
                vkd3d_atomic_uint64_store_explicit(&block->r.va, min_va, vkd3d_memory_order_relaxed);
                vkd3d_atomic_ptr_store_explicit(&block->r.resource, resource, vkd3d_memory_order_relaxed);
            }
            else
            {
                vkd3d_atomic_uint64_store_explicit(&block->l.va, max_va, vkd3d_memory_order_relaxed);
                vkd3d_atomic_ptr_store_explicit(&block->l.resource, resource, vkd3d_memory_order_relaxed);
            }

            block_va += VKD3D_VA_BLOCK_SIZE;
        }
    }
    else
    {
        pthread_mutex_lock(&va_map->mutex);

        if (!vkd3d_va_map_find_small_entry(va_map, resource->va, &index))
        {
            vkd3d_array_reserve((void**)&va_map->small_entries, &va_map->small_entries_size,
                    va_map->small_entries_count + 1, sizeof(*va_map->small_entries));

            memmove(&va_map->small_entries[index + 1], &va_map->small_entries[index],
                    sizeof(*va_map->small_entries) * (va_map->small_entries_count - index));

            va_map->small_entries[index] = resource;
            va_map->small_entries_count += 1;
        }

        pthread_mutex_unlock(&va_map->mutex);
    }
}

void vkd3d_va_map_remove(struct vkd3d_va_map *va_map, const struct vkd3d_unique_resource *resource)
{
    VkDeviceAddress block_va, min_va, max_va;
    struct vkd3d_va_block *block;
    size_t index;

    if (resource->size >= VKD3D_VA_BLOCK_SIZE)
    {
        min_va = resource->va;
        max_va = resource->va + resource->size;
        block_va = min_va & ~VKD3D_VA_LO_MASK;

        while (block_va < max_va)
        {
            block = vkd3d_va_map_get_block(va_map, block_va);

            if (vkd3d_atomic_ptr_load_explicit(&block->l.resource, vkd3d_memory_order_relaxed) == resource)
            {
                vkd3d_atomic_uint64_store_explicit(&block->l.va, 0, vkd3d_memory_order_relaxed);
                vkd3d_atomic_ptr_store_explicit(&block->l.resource, NULL, vkd3d_memory_order_relaxed);
            }
            else if (vkd3d_atomic_ptr_load_explicit(&block->r.resource, vkd3d_memory_order_relaxed) == resource)
            {
                vkd3d_atomic_uint64_store_explicit(&block->r.va, 0, vkd3d_memory_order_relaxed);
                vkd3d_atomic_ptr_store_explicit(&block->r.resource, NULL, vkd3d_memory_order_relaxed);
            }

            block_va += VKD3D_VA_BLOCK_SIZE;
        }
    }
    else
    {
        pthread_mutex_lock(&va_map->mutex);

        if (vkd3d_va_map_find_small_entry(va_map, resource->va, &index) == resource)
        {
            va_map->small_entries_count -= 1;

            memmove(&va_map->small_entries[index], &va_map->small_entries[index + 1],
                    sizeof(*va_map->small_entries) * (va_map->small_entries_count - index));
        }

        pthread_mutex_unlock(&va_map->mutex);
    }
}

static struct vkd3d_unique_resource *vkd3d_va_map_deref_mutable(struct vkd3d_va_map *va_map, VkDeviceAddress va)
{
    struct vkd3d_va_block *block = vkd3d_va_map_find_block(va_map, va);
    struct vkd3d_unique_resource *resource = NULL;

    if (block)
    {
        if (va < vkd3d_atomic_uint64_load_explicit(&block->l.va, vkd3d_memory_order_relaxed))
            resource = vkd3d_atomic_ptr_load_explicit(&block->l.resource, vkd3d_memory_order_relaxed);
        else if (va >= vkd3d_atomic_uint64_load_explicit(&block->r.va, vkd3d_memory_order_relaxed))
            resource = vkd3d_atomic_ptr_load_explicit(&block->r.resource, vkd3d_memory_order_relaxed);
    }

    if (!resource)
    {
        pthread_mutex_lock(&va_map->mutex);
        resource = vkd3d_va_map_find_small_entry(va_map, va, NULL);
        pthread_mutex_unlock(&va_map->mutex);
    }

    return resource;
}

const struct vkd3d_unique_resource *vkd3d_va_map_deref(struct vkd3d_va_map *va_map, VkDeviceAddress va)
{
    return vkd3d_va_map_deref_mutable(va_map, va);
}

void vkd3d_va_map_try_read_rtas(struct vkd3d_va_map *va_map,
        struct d3d12_device *device, VkDeviceAddress va,
        VkAccelerationStructureKHR *acceleration_structure,
        enum vkd3d_rtas_kind *rtas_kind)
{
    const struct vkd3d_unique_resource *resource;
    struct vkd3d_view_map *view_map;
    const struct vkd3d_view *view;
    struct vkd3d_view_key key;

    *acceleration_structure = VK_NULL_HANDLE;
    *rtas_kind = VKD3D_RTAS_KIND_UNKNOWN;

    resource = vkd3d_va_map_deref(va_map, va);
    if (!resource || !resource->va)
        return;

    view_map = vkd3d_atomic_ptr_load_explicit(&resource->view_map, vkd3d_memory_order_acquire);
    if (!view_map)
        return;

    key.view_type = VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE;
    key.u.buffer.buffer = resource->vk_buffer;
    key.u.buffer.offset = va - resource->va;
    key.u.buffer.size = resource->size - key.u.buffer.offset;
    key.u.buffer.format = NULL;
    key.u.buffer.usage = 0;

    view = vkd3d_view_map_get_view(view_map, device, &key);
    if (!view)
        return;

    *acceleration_structure = view->vk_acceleration_structure;
    *rtas_kind = (enum vkd3d_rtas_kind)
        vkd3d_atomic_uint32_load_explicit(&view->info.buffer.rtas_kind, vkd3d_memory_order_relaxed);
}

const char *vkd3d_get_rtas_kind_string(enum vkd3d_rtas_kind rtas_kind)
{
    switch (rtas_kind)
    {
        case VKD3D_RTAS_KIND_UNKNOWN: return "Unknown";
        case VKD3D_RTAS_KIND_TLAS: return "TLAS";
        case VKD3D_RTAS_KIND_NON_TLAS: return "non-TLAS";
        case VKD3D_RTAS_KIND_MUTATED: return "mutated";
        default: return "**Invalid**";
    }
}

VkAccelerationStructureKHR vkd3d_va_map_place_acceleration_structure(struct vkd3d_va_map *va_map,
        struct d3d12_device *device,
        VkDeviceAddress va,
        enum vkd3d_rtas_kind rtas_kind)
{
    enum vkd3d_rtas_kind previous_rtas_kind, expected_rtas_kind, desired_rtas_kind;
    struct vkd3d_unique_resource *resource;
    struct vkd3d_view_map *old_view_map;
    struct vkd3d_view_map *view_map;
    struct vkd3d_view_key key;
    struct vkd3d_view *view;

    resource = vkd3d_va_map_deref_mutable(va_map, va);
    if (!resource || !resource->va)
        return VK_NULL_HANDLE;

    view_map = vkd3d_atomic_ptr_load_explicit(&resource->view_map, vkd3d_memory_order_acquire);
    if (!view_map)
    {
        /* This is the first time we attempt to place an AS on top of this allocation, so
         * CAS in a pointer. */
        view_map = vkd3d_malloc(sizeof(*view_map));
        if (!view_map)
            return VK_NULL_HANDLE;

        if (FAILED(vkd3d_view_map_init(view_map)))
        {
            vkd3d_free(view_map);
            return VK_NULL_HANDLE;
        }

        /* Need to release in case other RTASes are placed at the same time, so they observe
         * the initialized view map, and need to acquire if some other thread placed it. */
        old_view_map = vkd3d_atomic_ptr_compare_exchange(&resource->view_map, NULL, view_map,
                vkd3d_memory_order_release, vkd3d_memory_order_acquire);
        if (old_view_map)
        {
            vkd3d_view_map_destroy(view_map, device);
            vkd3d_free(view_map);
            view_map = old_view_map;
        }
    }

    key.view_type = VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE;
    key.u.buffer.buffer = resource->vk_buffer;
    key.u.buffer.offset = va - resource->va;
    key.u.buffer.size = resource->size - key.u.buffer.offset;
    key.u.buffer.format = NULL;
    key.u.buffer.usage = 0;

    view = vkd3d_view_map_create_view2(view_map, device, &key, rtas_kind);
    if (!view)
        return VK_NULL_HANDLE;

    /* Atomic CAS loop maintaining the rtas_kind state machine:
     *   UNKNOWN -> TLAS | NON_TLAS  (upgrade on first known place)
     *   TLAS | NON_TLAS -> MUTATED  (collapse on cross-kind place)
     *   MUTATED                     (sticky, terminal)
     * place(UNKNOWN) is a no-op and never overwrites an established kind. */
    previous_rtas_kind = (enum vkd3d_rtas_kind)
            vkd3d_atomic_uint32_load_explicit(&view->info.buffer.rtas_kind, vkd3d_memory_order_acquire);
    for (;;)
    {
        expected_rtas_kind = previous_rtas_kind;
        desired_rtas_kind = rtas_kind == VKD3D_RTAS_KIND_UNKNOWN ? previous_rtas_kind : rtas_kind;

        if (rtas_kind != previous_rtas_kind &&
                previous_rtas_kind != VKD3D_RTAS_KIND_UNKNOWN &&
                rtas_kind != VKD3D_RTAS_KIND_UNKNOWN)
            desired_rtas_kind = VKD3D_RTAS_KIND_MUTATED;

        if (desired_rtas_kind == previous_rtas_kind)
            break;

        previous_rtas_kind = vkd3d_atomic_uint32_compare_exchange(&view->info.buffer.rtas_kind, expected_rtas_kind,
                desired_rtas_kind, vkd3d_memory_order_release, vkd3d_memory_order_acquire);
        if (previous_rtas_kind == expected_rtas_kind)
        {
            if (desired_rtas_kind == VKD3D_RTAS_KIND_MUTATED)
            {
                WARN("Attempted to place %s on VA #%"PRIx64" previously used by %s, marking mutated.\n",
                        vkd3d_get_rtas_kind_string(rtas_kind), va,
                        vkd3d_get_rtas_kind_string(previous_rtas_kind));
            }
            break;
        }
    }

    return view->vk_acceleration_structure;
}

void vkd3d_va_map_init(struct vkd3d_va_map *va_map)
{
    memset(va_map, 0, sizeof(*va_map));
    pthread_mutex_init(&va_map->mutex, NULL);
}

void vkd3d_va_map_cleanup(struct vkd3d_va_map *va_map)
{
    vkd3d_va_map_cleanup_tree(&va_map->va_tree);
    pthread_mutex_destroy(&va_map->mutex);
    vkd3d_free(va_map->small_entries);
    vkd3d_free(va_map->resource_mappings);
    vkd3d_free(va_map->sampler_mappings);
}

void vkd3d_va_map_insert_descriptor_heap(struct vkd3d_va_map *va_map,
        uintptr_t va, size_t range, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    struct vkd3d_descriptor_heap_mapping *mapping;
    pthread_mutex_lock(&va_map->mutex);

    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        vkd3d_array_reserve((void **)&va_map->resource_mappings, &va_map->resource_mappings_size,
            va_map->resource_mappings_count + 1, sizeof(*va_map->resource_mappings));
        mapping = &va_map->resource_mappings[va_map->resource_mappings_count++];
    }
    else
    {
        vkd3d_array_reserve((void **)&va_map->sampler_mappings, &va_map->sampler_mappings_size,
            va_map->sampler_mappings_count + 1, sizeof(*va_map->sampler_mappings));
        mapping = &va_map->sampler_mappings[va_map->sampler_mappings_count++];
    }

    mapping->va = va;
    mapping->range = range;

    pthread_mutex_unlock(&va_map->mutex);
}

void vkd3d_va_map_remove_descriptor_heap(struct vkd3d_va_map *va_map,
        uintptr_t va, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    size_t i;

    pthread_mutex_lock(&va_map->mutex);

    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        for (i = 0; i < va_map->resource_mappings_count; i++)
        {
            if (va_map->resource_mappings[i].va == va)
            {
                va_map->resource_mappings[i] = va_map->resource_mappings[--va_map->resource_mappings_count];
                break;
            }
        }
    }
    else
    {
        for (i = 0; i < va_map->sampler_mappings_count; i++)
        {
            if (va_map->sampler_mappings[i].va == va)
            {
                va_map->sampler_mappings[i] = va_map->sampler_mappings[--va_map->sampler_mappings_count];
                break;
            }
        }
    }

    pthread_mutex_unlock(&va_map->mutex);
}

size_t vkd3d_va_map_query_descriptor_heap_offset(struct vkd3d_va_map *va_map,
        uintptr_t va, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    const struct vkd3d_descriptor_heap_mapping *mappings = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            ? va_map->resource_mappings : va_map->sampler_mappings;
    size_t ret = SIZE_MAX;
    size_t count;
    size_t i;

    pthread_mutex_lock(&va_map->mutex);

    count = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ?
            va_map->resource_mappings_count : va_map->sampler_mappings_count;

    /* We don't expect there to be that many shader visible descriptor heaps live on the device,
     * so a simple linear search is perfectly fine. */
    for (i = 0; i < count; i++)
    {
        if (va >= mappings[i].va && va < mappings[i].va + mappings[i].range)
        {
            ret = va - mappings[i].va;
            break;
        }
    }

    pthread_mutex_unlock(&va_map->mutex);
    return ret;
}