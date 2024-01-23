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
#include "vkd3d_profiling.h"

#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <time.h>
#endif

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#endif

#define DIV_ROUND_UP(a, b) ((a) % (b) == 0 ? (a) / (b) : (a) / (b) + 1)

#define STATIC_ASSERT(e) extern void __VKD3D_STATIC_ASSERT__(int [(e) ? 1 : -1])

#define MEMBER_SIZE(t, m) sizeof(((t *)0)->m)

static inline uint64_t align64(uint64_t addr, uint64_t alignment)
{
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
    return (addr + (alignment - 1)) & ~(alignment - 1);
}

static inline size_t align(size_t addr, size_t alignment)
{
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
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
#elif defined(__GNUC__) || defined(__clang__)
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
    return _BitScanForward(&result, mask) ? result : 32;
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

static inline unsigned int vkd3d_bitmask_iter32(uint32_t *mask)
{
    uint32_t cur_mask = *mask;
    *mask = cur_mask & (cur_mask - 1);
    return vkd3d_bitmask_tzcnt32(cur_mask);
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
#elif defined(__GNUC__) || defined(__clang__)
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

static inline unsigned int vkd3d_log2i_ceil(unsigned int x)
{
    if (x == 1)
        return 0;
    else
        return vkd3d_log2i(x - 1) + 1;
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

static inline void vkd3d_parse_version(const char *version, int *major, int *minor, int *patch)
{
    char *end;

    *major = strtol(version, &end, 10);
    version = end;
    if (*version == '.')
        ++version;
    *minor = strtol(version, &end, 10);
    version = end;
    if (*version == '.')
        ++version;
    *patch = strtol(version, NULL, 10);
}

static inline uint32_t float_bits_to_uint32(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static inline size_t vkd3d_wcslen(const WCHAR *wstr)
{
    size_t length = 0;

    while (true)
    {
        if (!wstr[length])
            return length;

        length += 1;
    }
}

static inline void *void_ptr_offset(void *ptr, size_t offset)
{
    return ((char*)ptr) + offset;
}

static inline int32_t vkd3d_float_to_fixed_24_8(float f)
{
    /* Docs suggest round to nearest even, but that does not match
     * observed behaviour. Always round away from zero instead. */
    return lroundf(f * 256.0f);
}

static inline float vkd3d_fixed_24_8_to_float(int32_t i)
{
    return (float)(i) / 256.0f;
}

#ifdef _MSC_VER
#define VKD3D_THREAD_LOCAL __declspec(thread)
#else
#define VKD3D_THREAD_LOCAL __thread
#endif

static inline uint64_t vkd3d_get_current_time_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER li, lf;
    uint64_t whole, part;
    QueryPerformanceCounter(&li);
    QueryPerformanceFrequency(&lf);
    whole = (li.QuadPart / lf.QuadPart) * 1000000000;
    part = ((li.QuadPart % lf.QuadPart) * 1000000000) / lf.QuadPart;
    return whole + part;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000000000ll + ts.tv_nsec;
#endif
}

#ifdef _MSC_VER
#pragma intrinsic(__rdtsc)
#endif

static inline uint64_t vkd3d_get_current_time_ticks(void)
{
#ifdef _MSC_VER
    return __rdtsc();
#elif defined(__i386__) || defined(__x86_64__)
    return __builtin_ia32_rdtsc();
#else
    return vkd3d_get_current_time_ns();
#endif
}

#if defined(__GNUC__) || defined(__clang__)
#define VKD3D_EXPECT_TRUE(x) __builtin_expect(!!(x), 1)
#define VKD3D_EXPECT_FALSE(x) __builtin_expect(!!(x), 0)
#else
#define VKD3D_EXPECT_TRUE(x) (x)
#define VKD3D_EXPECT_FALSE(x) (x)
#endif

#endif  /* __VKD3D_COMMON_H */
