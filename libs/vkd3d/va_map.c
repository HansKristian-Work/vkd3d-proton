/*
 * Copyright 2020 Philip Rebohle for Valve Software
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
        tree = tree->next[next_address & VKD3D_VA_NEXT_MASK];
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

        if (!(tree = *tree_ptr))
        {
            void *orig;
            tree = vkd3d_malloc(sizeof(*tree));
            memset(tree, 0, sizeof(*tree));

            orig = vkd3d_atomic_ptr_compare_exchange(tree_ptr, NULL, tree, vkd3d_memory_order_release, vkd3d_memory_order_relaxed);

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

void vkd3d_va_map_insert(struct vkd3d_va_map *va_map, struct d3d12_resource *resource)
{
    VkDeviceAddress block_va, min_va, max_va;
    struct vkd3d_va_block *block;

    min_va = resource->gpu_address;
    max_va = resource->gpu_address + resource->gpu_size;
    block_va = min_va & ~VKD3D_VA_LO_MASK;

    while (block_va < max_va)
    {
        block = vkd3d_va_map_get_block(va_map, block_va);

        if (block_va < min_va)
        {
            block->r.va = min_va;
            block->r.resource = resource;
        }
        else
        {
            block->l.va = max_va;
            block->l.resource = resource;
        }

        block_va += VKD3D_VA_BLOCK_SIZE;
    }
}

void vkd3d_va_map_remove(struct vkd3d_va_map *va_map, struct d3d12_resource *resource)
{
    VkDeviceAddress block_va, min_va, max_va;
    struct vkd3d_va_block *block;

    min_va = resource->gpu_address;
    max_va = resource->gpu_address + resource->gpu_size;
    block_va = min_va & ~VKD3D_VA_LO_MASK;

    while (block_va < max_va)
    {
        block = vkd3d_va_map_get_block(va_map, block_va);

        if (block->l.resource == resource)
        {
            block->l.va = 0;
            block->l.resource = NULL;
        }
        else if (block->r.resource == resource)
        {
            block->r.va = 0;
            block->r.resource = NULL;
        }

        block_va += VKD3D_VA_BLOCK_SIZE;
    }
}

struct d3d12_resource *vkd3d_va_map_deref(const struct vkd3d_va_map *va_map, VkDeviceAddress va)
{
    const struct vkd3d_va_block *block = vkd3d_va_map_find_block(va_map, va);

    if (block)
    {
        if (va <  block->l.va) return block->l.resource;
        if (va >= block->r.va) return block->r.resource;
    }

    return NULL;
}

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
    new_range.size = size;

    range_idx = allocator->free_range_count;
    range_shift = 0;

    /* Find and effectively delete any free range adjacent to new_range */
    for (i = 0; i < allocator->free_range_count; i++)
    {
        const struct vkd3d_va_range *cur_range = &allocator->free_ranges[i];

        if (range_shift)
            allocator->free_ranges[i - range_shift] = *cur_range;

        if (cur_range->base == va + size || cur_range->base + cur_range->size == va)
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
    pthread_mutex_init(&va_map->va_allocator.mutex, NULL);

    /* Make sure we never return 0 as a valid VA */
    va_map->va_allocator.next_va = VKD3D_VA_BLOCK_SIZE;
}

void vkd3d_va_map_cleanup(struct vkd3d_va_map *va_map)
{
    vkd3d_va_map_cleanup_tree(&va_map->va_tree);

    pthread_mutex_destroy(&va_map->va_allocator.mutex);
    vkd3d_free(va_map->va_allocator.free_ranges);
}

