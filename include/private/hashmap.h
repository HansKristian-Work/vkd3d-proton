/*
 * Hash map support
 *
 * Copyright 2020 Philip Rebohle for Valve Corporation
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

#ifndef __VKD3D_HASHMAP_H
#define __VKD3D_HASHMAP_H

#include <stddef.h>

#include "memory.h"

enum hash_map_entry_flag
{
    HASH_MAP_ENTRY_OCCUPIED = (1 << 0),
};

struct hash_map_entry
{
    uint32_t hash_value;
    uint32_t flags;
};

typedef uint32_t (*pfn_hash_func)(const void* key);
typedef bool (*pfn_hash_compare_func)(const void *key, const struct hash_map_entry *entry);

/* Open-addressing hash table */
struct hash_map
{
    pfn_hash_func hash_func;
    pfn_hash_compare_func compare_func;
    void *entries;
    size_t entry_size;
    uint32_t entry_count;
    uint32_t used_count;
};

static inline struct hash_map_entry *hash_map_get_entry(struct hash_map *hash_map, uint32_t entry_idx)
{
    return void_ptr_offset(hash_map->entries, hash_map->entry_size * entry_idx);
}

static inline uint32_t hash_map_get_entry_idx(struct hash_map *hash_map, uint32_t hash_value)
{
    return hash_value % hash_map->entry_count;
}

static inline uint32_t hash_map_next_entry_idx(struct hash_map *hash_map, uint32_t entry_idx)
{
    uint32_t next_idx = entry_idx + 1;
    return next_idx < hash_map->entry_count ? next_idx : 0;
}

static inline uint32_t hash_map_next_size(uint32_t old_size)
{
    /* This yields a sequence of primes and numbers with two
     * relatively large prime factors for any reasonable hash
     * table size */
    return old_size ? (old_size * 2 + 5) : 37;
}

static inline bool hash_map_grow(struct hash_map *hash_map)
{
    uint32_t i, old_count, new_count;
    void *new_entries, *old_entries;

    old_count = hash_map->entry_count;
    old_entries = hash_map->entries;

    new_count = hash_map_next_size(hash_map->entry_count);

    if (!(new_entries = vkd3d_calloc(new_count, hash_map->entry_size)))
        return false;

    hash_map->entry_count = new_count;
    hash_map->entries = new_entries;

    for (i = 0; i < old_count; i++)
    {
        /* Relocate existing entries one by one */
        struct hash_map_entry *old_entry = void_ptr_offset(old_entries, i * hash_map->entry_size);

        if (old_entry->flags & HASH_MAP_ENTRY_OCCUPIED)
        {
            uint32_t entry_idx = hash_map_get_entry_idx(hash_map, old_entry->hash_value);
            struct hash_map_entry *new_entry = hash_map_get_entry(hash_map, entry_idx);

            while (new_entry->flags & HASH_MAP_ENTRY_OCCUPIED)
            {
                entry_idx = hash_map_next_entry_idx(hash_map, entry_idx);
                new_entry = hash_map_get_entry(hash_map, entry_idx);
            }

            memcpy(new_entry, old_entry, hash_map->entry_size);
        }
    }

    vkd3d_free(old_entries);
    return true;
}

static inline bool hash_map_should_grow_before_insert(struct hash_map *hash_map)
{
    /* Allow a load factor of 0.7 for performance reasons */
    return 10 * hash_map->used_count >= 7 * hash_map->entry_count;
}

static inline struct hash_map_entry *hash_map_find(struct hash_map *hash_map, const void *key)
{
    uint32_t hash_value, entry_idx;
    
    if (!hash_map->entries)
        return NULL;

    hash_value = hash_map->hash_func(key);
    entry_idx = hash_map_get_entry_idx(hash_map, hash_value);

    /* We never allow the hash table to be completely
     * populated, so this is guaranteed to return */
    while (true)
    {
        struct hash_map_entry *entry = hash_map_get_entry(hash_map, entry_idx);

        if (!(entry->flags & HASH_MAP_ENTRY_OCCUPIED))
            return NULL;

        if (entry->hash_value == hash_value && hash_map->compare_func(key, entry))
            return entry;

        entry_idx = hash_map_next_entry_idx(hash_map, entry_idx);
    }
}

static inline struct hash_map_entry *hash_map_insert(struct hash_map *hash_map, const void *key, const struct hash_map_entry *entry)
{
    struct hash_map_entry *target = NULL;
    uint32_t hash_value, entry_idx;

    if (hash_map_should_grow_before_insert(hash_map))
    {
        if (!hash_map_grow(hash_map))
            return NULL;
    }

    hash_value = hash_map->hash_func(key);
    entry_idx = hash_map_get_entry_idx(hash_map, hash_value);

    while (!target)
    {
        struct hash_map_entry *current = hash_map_get_entry(hash_map, entry_idx);

        if (!(current->flags & HASH_MAP_ENTRY_OCCUPIED) ||
                (current->hash_value == hash_value && hash_map->compare_func(key, entry)))
            target = current;

        entry_idx = hash_map_next_entry_idx(hash_map, entry_idx);
    }

    if (!(target->flags & HASH_MAP_ENTRY_OCCUPIED))
        hash_map->used_count += 1;

    memcpy(target, entry, hash_map->entry_size);
    target->flags = HASH_MAP_ENTRY_OCCUPIED;
    target->hash_value = hash_value;
    return target;
}

static inline void hash_map_init(struct hash_map *hash_map, pfn_hash_func hash_func, pfn_hash_compare_func compare_func, size_t entry_size)
{
    hash_map->hash_func = hash_func;
    hash_map->compare_func = compare_func;
    hash_map->entries = NULL;
    hash_map->entry_size = entry_size;
    hash_map->entry_count = 0;
    hash_map->used_count = 0;
}

static inline void hash_map_clear(struct hash_map *hash_map)
{
    vkd3d_free(hash_map->entries);
    hash_map->entries = NULL;
    hash_map->entry_count = 0;
    hash_map->used_count = 0;
}

static inline uint32_t hash_combine(uint32_t old_hash, uint32_t new_hash) {
    return old_hash ^ (new_hash + 0x9e3779b9 + (old_hash << 6) + (old_hash >> 2));
}

static inline uint32_t hash_uint64(uint64_t n)
{
    return hash_combine((uint32_t)n, (uint32_t)(n >> 32));
}

#endif  /* __VKD3D_HASHMAP_H */
