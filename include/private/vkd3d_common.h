/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#ifndef __VKD3D_COMMON_H
#define __VKD3D_COMMON_H

#include "vkd3d_windows.h"
#include "vkd3d_spinlock.h"

#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#endif

#define DIV_ROUND_UP(a, b) ((a) % (b) == 0 ? (a) / (b) : (a) / (b) + 1)

#define STATIC_ASSERT(e) extern void __VKD3D_STATIC_ASSERT__(int [(e) ? 1 : -1])

#define MEMBER_SIZE(t, m) sizeof(((t *)0)->m)

static inline size_t align(size_t addr, size_t alignment)
{
    return (addr + (alignment - 1)) & ~(alignment - 1);
}

#ifdef __GNUC__
# define VKD3D_PRINTF_FUNC(fmt, args) __attribute__((format(printf, fmt, args)))
# define VKD3D_UNUSED __attribute__((unused))
#else
# define VKD3D_PRINTF_FUNC(fmt, args)
# define VKD3D_UNUSED
#endif  /* __GNUC__ */

static inline unsigned int vkd3d_popcount(unsigned int v)
{
#ifdef _MSC_VER
    return __popcnt(v);
#elif defined(HAVE_BUILTIN_POPCOUNT)
    return __builtin_popcount(v);
#else
    v -= (v >> 1) & 0x55555555;
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    return (((v + (v >> 4)) & 0x0f0f0f0f) * 0x01010101) >> 24;
#endif
}

static inline bool vkd3d_bitmask_is_contiguous(unsigned int mask)
{
    unsigned int i, j;

    for (i = 0, j = 0; i < sizeof(mask) * CHAR_BIT; ++i)
    {
        if (mask & (1u << i))
            ++j;
        else if (j)
            break;
    }

    return vkd3d_popcount(mask) == j;
}

/* Returns 64 for mask == 0 */
static inline unsigned int vkd3d_bitmask_tzcnt64(uint64_t mask)
{
#ifdef _MSC_VER
    unsigned long result;
#ifdef _WIN64
    return _BitScanForward64(&result, mask) ? result : 64;
#else
    uint32_t lower, upper;
    lower = (uint32_t)mask;
    upper = (uint32_t)(mask >> 32);
    if (_BitScanForward(&result, lower))
        return result;
    else if (_BitScanForward(&result, upper))
        return result + 32;
    else
        return 64;
#endif
#elif defined(__GNUC__) || defined(__clang__)
    return mask ? __builtin_ctzll(mask) : 64;
#else
    #error "No implementation for ctzll."
#endif
}

/* Returns 32 for mask == 0 */
static inline unsigned int vkd3d_bitmask_tzcnt32(uint32_t mask)
{
#ifdef _MSC_VER
    unsigned long result;
    _BitScanForward(&result, mask) ? result : 32;
    return result;
#elif defined(__GNUC__) || defined(__clang__)
    return mask ? __builtin_ctz(mask) : 32;
#else
    #error "No implementation for ctz."
#endif
}

/* find least significant bit, then remove that bit from mask */
static inline unsigned int vkd3d_bitmask_iter64(uint64_t* mask)
{
    uint64_t cur_mask = *mask;
    *mask = cur_mask & (cur_mask - 1);
    return vkd3d_bitmask_tzcnt64(cur_mask);
}

struct vkd3d_bitmask_range
{
    unsigned int offset;
    unsigned int count;
};

static inline struct vkd3d_bitmask_range vkd3d_bitmask_iter32_range(uint32_t *mask)
{
    struct vkd3d_bitmask_range range;
    uint32_t tmp;

    if (*mask == ~0u)
    {
        range.offset = 0;
        range.count = 32;
        *mask = 0u;
    }
    else
    {
        range.offset = vkd3d_bitmask_tzcnt32(*mask);
        tmp = *mask >> range.offset;
        range.count = vkd3d_bitmask_tzcnt32(~tmp);
        *mask &= ~(((1u << range.count) - 1u) << range.offset);
    }

    return range;
}

/* Undefined for x == 0. */
static inline unsigned int vkd3d_log2i(unsigned int x)
{
#ifdef _MSC_VER
    /* _BitScanReverse returns the index of the highest set bit,
     * unlike clz which is 31 - index. */
    unsigned long result;
    _BitScanReverse(&result, x);
    return (unsigned int)result;
#elif defined(HAVE_BUILTIN_CLZ)
    return __builtin_clz(x) ^ 0x1f;
#else
    static const unsigned int l[] =
    {
        ~0u, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
          4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
          5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
          5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
          7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    };
    unsigned int i;

    return (i = x >> 16) ? (x = i >> 8) ? l[x] + 24
            : l[i] + 16 : (i = x >> 8) ? l[i] + 8 : l[x];
#endif
}

static inline int ascii_isupper(int c)
{
    return 'A' <= c && c <= 'Z';
}

static inline int ascii_tolower(int c)
{
    return ascii_isupper(c) ? c - 'A' + 'a' : c;
}

static inline int ascii_strcasecmp(const char *a, const char *b)
{
    int c_a, c_b;

    do
    {
        c_a = ascii_tolower(*a++);
        c_b = ascii_tolower(*b++);
    } while (c_a == c_b && c_a != '\0');

    return c_a - c_b;
}

static inline bool is_power_of_two(unsigned int x)
{
    return x && !(x & (x -1));
}

static inline void vkd3d_parse_version(const char *version, int *major, int *minor)
{
    *major = atoi(version);

    while (isdigit(*version))
        ++version;
    if (*version == '.')
        ++version;

    *minor = atoi(version);
}

#endif  /* __VKD3D_COMMON_H */
