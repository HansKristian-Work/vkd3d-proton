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
        VkMicromapEXT *micromap)
{
    const struct vkd3d_unique_resource *resource;
    struct vkd3d_view_map *view_map;
    const struct vkd3d_view *view;
    struct vkd3d_view_key key;

    *acceleration_structure = VK_NULL_HANDLE;
    *micromap = VK_NULL_HANDLE;

    resource = vkd3d_va_map_deref(va_map, va);
    if (!resource || !resource->va)
        return;

    view_map = vkd3d_atomic_ptr_load_explicit(&resource->view_map, vkd3d_memory_order_acquire);
    if (!view_map)
        return;

    key.view_type = VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE_OR_OPACITY_MICROMAP;
    key.u.buffer.buffer = resource->vk_buffer;
    key.u.buffer.offset = va - resource->va;
    key.u.buffer.size = resource->size - key.u.buffer.offset;
    key.u.buffer.format = NULL;

    view = vkd3d_view_map_get_view(view_map, device, &key);
    if (!view)
        return;
    
    if (view->info.buffer.rtas_is_micromap)
        *micromap = view->vk_micromap;
    else
        *acceleration_structure = view->vk_acceleration_structure;
}

static void vkd3d_va_map_try_place_rtas(struct vkd3d_va_map *va_map,
        struct d3d12_device *device, VkDeviceAddress va, bool rtas_is_omm,
        VkAccelerationStructureKHR *acceleration_structure,
        VkMicromapEXT *micromap)
{
    struct vkd3d_unique_resource *resource;
    struct vkd3d_view_map *old_view_map;
    struct vkd3d_view_map *view_map;
    const struct vkd3d_view *view;
    struct vkd3d_view_key key;

    *acceleration_structure = VK_NULL_HANDLE;
    *micromap = VK_NULL_HANDLE;

    resource = vkd3d_va_map_deref_mutable(va_map, va);
    if (!resource || !resource->va)
        return;

    view_map = vkd3d_atomic_ptr_load_explicit(&resource->view_map, vkd3d_memory_order_acquire);
    if (!view_map)
    {
        /* This is the first time we attempt to place an AS on top of this allocation, so
         * CAS in a pointer. */
        view_map = vkd3d_malloc(sizeof(*view_map));
        if (!view_map)
            return;

        if (FAILED(vkd3d_view_map_init(view_map)))
        {
            vkd3d_free(view_map);
            return;
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

    key.view_type = VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE_OR_OPACITY_MICROMAP;
    key.u.buffer.buffer = resource->vk_buffer;
    key.u.buffer.offset = va - resource->va;
    key.u.buffer.size = resource->size - key.u.buffer.offset;
    key.u.buffer.format = NULL;

    view = vkd3d_view_map_create_view2(view_map, device, &key, rtas_is_omm);
    if (!view)
        return;
    
    if (view->info.buffer.rtas_is_micromap)
        *micromap = view->vk_micromap;
    else
        *acceleration_structure = view->vk_acceleration_structure;
}

VkAccelerationStructureKHR vkd3d_va_map_place_acceleration_structure(struct vkd3d_va_map *va_map,
        struct d3d12_device *device,
        VkDeviceAddress va)
{
    VkAccelerationStructureKHR acceleration_structure;
    VkMicromapEXT micromap;

    vkd3d_va_map_try_place_rtas(va_map, device, va, false, &acceleration_structure, &micromap);

    if (micromap)
        FIXME("Attempted to place RTAS on VA #%"PRIx64" previously used by OMM.\n", va);

    return acceleration_structure;
}

VkMicromapEXT vkd3d_va_map_place_opacity_micromap(struct vkd3d_va_map *va_map,
        struct d3d12_device *device,
        VkDeviceAddress va)
{
    VkAccelerationStructureKHR acceleration_structure;
    VkMicromapEXT micromap;

    vkd3d_va_map_try_place_rtas(va_map, device, va, true, &acceleration_structure, &micromap);

    if (acceleration_structure)
        FIXME("Attempted to place OMM on VA #%"PRIx64" previously used by RTAS.\n", va);

    return micromap;
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
}

