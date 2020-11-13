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

static const struct vkd3d_va_block *vkd3d_va_map_find_block(const struct vkd3d_va_map *va_map, VkDeviceAddress va)
{
    VkDeviceAddress next_address = vkd3d_va_map_get_next_address(va);
    const struct vkd3d_va_tree *tree = &va_map->va_tree;

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

static const struct vkd3d_unique_resource *vkd3d_va_map_find_small_entry(struct vkd3d_va_map *va_map,
        VkDeviceAddress va, size_t *index)
{
    const struct vkd3d_unique_resource *resource = NULL;
    size_t hi = va_map->small_entries_count;
    size_t lo = 0;

    while (lo < hi)
    {
        const struct vkd3d_unique_resource *r;
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

void vkd3d_va_map_insert(struct vkd3d_va_map *va_map, const struct vkd3d_unique_resource *resource)
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

const struct vkd3d_unique_resource *vkd3d_va_map_deref(struct vkd3d_va_map *va_map, VkDeviceAddress va)
{
    const struct vkd3d_va_block *block = vkd3d_va_map_find_block(va_map, va);
    const struct vkd3d_unique_resource *resource = NULL;

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

#define VKD3D_FAKE_VA_ALIGNMENT (65536)

VkDeviceAddress vkd3d_va_map_alloc_fake_va(struct vkd3d_va_map *va_map, VkDeviceSize size)
{
    struct vkd3d_va_allocator *allocator = &va_map->va_allocator;
    struct vkd3d_va_range range;
    VkDeviceAddress va;
    size_t i;
    int rc;

    if ((rc = pthread_mutex_lock(&allocator->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return 0;
    }

    memset(&range, 0, sizeof(range));
    size = align(size, VKD3D_FAKE_VA_ALIGNMENT);

    /* The free list is ordered in such a way that the largest range
     * is always first, so we don't have to iterate over the list */
    if (allocator->free_range_count)
        range = allocator->free_ranges[0];

    if (range.size >= size)
    {
        va = range.base;

        range.base += size;
        range.size -= size;

        for (i = 0; i < allocator->free_range_count - 1; i++)
        {
            if (allocator->free_ranges[i + 1].size > range.size)
                allocator->free_ranges[i] = allocator->free_ranges[i + 1];
            else
                break;
        }

        if (range.size)
            allocator->free_ranges[i] = range;
        else
            allocator->free_range_count--;
    }
    else
    {
        va = allocator->next_va;
        allocator->next_va += size;
    }

    pthread_mutex_unlock(&allocator->mutex);
    return va;
}

void vkd3d_va_map_free_fake_va(struct vkd3d_va_map *va_map, VkDeviceAddress va, VkDeviceSize size)
{
    struct vkd3d_va_allocator *allocator = &va_map->va_allocator;
    size_t range_idx, range_shift, i;
    struct vkd3d_va_range new_range;
    int rc;

    if ((rc = pthread_mutex_lock(&allocator->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return;
    }

    new_range.base = va;
    new_range.size = align(size, VKD3D_FAKE_VA_ALIGNMENT);

    range_idx = allocator->free_range_count;
    range_shift = 0;

    /* Find and effectively delete any free range adjacent to new_range */
    for (i = 0; i < allocator->free_range_count; i++)
    {
        const struct vkd3d_va_range *cur_range = &allocator->free_ranges[i];

        if (range_shift)
            allocator->free_ranges[i - range_shift] = *cur_range;

        if (cur_range->base == new_range.base + new_range.size || cur_range->base + cur_range->size == new_range.base)
        {
            if (range_idx == allocator->free_range_count)
                range_idx = i;
            else
                range_shift++;

            new_range.base = min(new_range.base, cur_range->base);
            new_range.size += cur_range->size;
        }
    }

    if (range_idx == allocator->free_range_count)
    {
        /* range_idx will be valid and point to the last element afterwards */
        if (!(vkd3d_array_reserve((void **)&allocator->free_ranges, &allocator->free_ranges_size,
                allocator->free_range_count + 1, sizeof(*allocator->free_ranges))))
        {
            ERR("Failed to add free range.\n");
            pthread_mutex_unlock(&allocator->mutex);
            return;
        }

        allocator->free_range_count += 1;
    }
    else
        allocator->free_range_count -= range_shift;

    /* Move ranges smaller than our new free range back to keep the list ordered */
    while (range_idx && allocator->free_ranges[range_idx - 1].size < new_range.size)
    {
        allocator->free_ranges[range_idx] = allocator->free_ranges[range_idx - 1];
        range_idx--;
    }

    allocator->free_ranges[range_idx] = new_range;
    pthread_mutex_unlock(&allocator->mutex);
}

void vkd3d_va_map_init(struct vkd3d_va_map *va_map)
{
    memset(va_map, 0, sizeof(*va_map));
    pthread_mutex_init(&va_map->mutex, NULL);
    pthread_mutex_init(&va_map->va_allocator.mutex, NULL);

    /* Make sure we never return 0 as a valid VA */
    va_map->va_allocator.next_va = VKD3D_VA_BLOCK_SIZE;
}

void vkd3d_va_map_cleanup(struct vkd3d_va_map *va_map)
{
    vkd3d_va_map_cleanup_tree(&va_map->va_tree);

    pthread_mutex_destroy(&va_map->va_allocator.mutex);
    pthread_mutex_destroy(&va_map->mutex);
    vkd3d_free(va_map->va_allocator.free_ranges);
    vkd3d_free(va_map->small_entries);
}

