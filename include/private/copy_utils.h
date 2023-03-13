/*
 * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_COPY_UTILS_H
#define __VKD3D_COPY_UTILS_H

#ifdef __SSE2__
#include <emmintrin.h>
#endif
#include <stdint.h>
#include <memory.h>

#ifdef __SSE2__

#define vkd3d_memcpy_aligned_64_non_temporal(dst, src) do { \
    __m128i a, b, c, d; \
    a = _mm_load_si128((const __m128i *)(src) + 0); \
    b = _mm_load_si128((const __m128i *)(src) + 1); \
    c = _mm_load_si128((const __m128i *)(src) + 2); \
    d = _mm_load_si128((const __m128i *)(src) + 3); \
    _mm_stream_si128((__m128i *)(dst) + 0, a); \
    _mm_stream_si128((__m128i *)(dst) + 1, b); \
    _mm_stream_si128((__m128i *)(dst) + 2, c); \
    _mm_stream_si128((__m128i *)(dst) + 3, d); \
} while(0)

#define vkd3d_memcpy_aligned_32_non_temporal(dst, src) do { \
    __m128i a, b; \
    a = _mm_load_si128((const __m128i *)(src) + 0); \
    b = _mm_load_si128((const __m128i *)(src) + 1); \
    _mm_stream_si128((__m128i *)(dst) + 0, a); \
    _mm_stream_si128((__m128i *)(dst) + 1, b); \
} while(0)

#define vkd3d_memcpy_aligned_16_non_temporal(dst, src) do { \
    __m128i a; \
    a = _mm_load_si128((const __m128i *)(src)); \
    _mm_stream_si128((__m128i *)(dst), a); \
} while(0)

#define vkd3d_memcpy_aligned_64_cached(dst, src) do { \
    __m128i a, b, c, d; \
    a = _mm_load_si128((const __m128i *)(src) + 0); \
    b = _mm_load_si128((const __m128i *)(src) + 1); \
    c = _mm_load_si128((const __m128i *)(src) + 2); \
    d = _mm_load_si128((const __m128i *)(src) + 3); \
    _mm_store_si128((__m128i *)(dst) + 0, a); \
    _mm_store_si128((__m128i *)(dst) + 1, b); \
    _mm_store_si128((__m128i *)(dst) + 2, c); \
    _mm_store_si128((__m128i *)(dst) + 3, d); \
} while(0)

#define vkd3d_memcpy_aligned_32_cached(dst, src) do { \
    __m128i a, b; \
    a = _mm_load_si128((const __m128i *)(src) + 0); \
    b = _mm_load_si128((const __m128i *)(src) + 1); \
    _mm_store_si128((__m128i *)(dst) + 0, a); \
    _mm_store_si128((__m128i *)(dst) + 1, b); \
} while(0)

#define vkd3d_memcpy_aligned_16_cached(dst, src) do { \
    __m128i a; \
    a = _mm_load_si128((const __m128i *)(src)); \
    _mm_store_si128((__m128i *)(dst), a); \
} while(0)

static inline void vkd3d_memcpy_aligned_non_temporal(void *dst_, const void *src_, size_t size)
{
    const uint8_t *src = src_;
    uint8_t *dst = dst_;
    size_t i;

    for (i = 0; i < size; i += 16)
        vkd3d_memcpy_aligned_16_non_temporal(dst + i, src + i);
}

static inline void vkd3d_memcpy_aligned_cached(void *dst_, const void *src_, size_t size)
{
    const uint8_t *src = src_;
    uint8_t *dst = dst_;
    size_t i;

    for (i = 0; i < size; i += 16)
        vkd3d_memcpy_aligned_16_cached(dst + i, src + i);
}

#define vkd3d_memcpy_non_temporal_barrier() _mm_sfence()
#else
#define vkd3d_memcpy_aligned_64_non_temporal(dst, src) memcpy((uint8_t *)(dst), (const uint8_t *)(src), 64)
#define vkd3d_memcpy_aligned_32_non_temporal(dst, src) memcpy((uint8_t *)(dst), (const uint8_t *)(src), 32)
#define vkd3d_memcpy_aligned_16_non_temporal(dst, src) memcpy((uint8_t *)(dst), (const uint8_t *)(src), 16)
#define vkd3d_memcpy_aligned_non_temporal(dst, src, size) memcpy((uint8_t *)(dst), (const uint8_t *)(src), size)
#define vkd3d_memcpy_aligned_64_cached(dst, src) memcpy((uint8_t *)(dst), (const uint8_t *)(src), 64)
#define vkd3d_memcpy_aligned_32_cached(dst, src) memcpy((uint8_t *)(dst), (const uint8_t *)(src), 32)
#define vkd3d_memcpy_aligned_16_cached(dst, src) memcpy((uint8_t *)(dst), (const uint8_t *)(src), 16)
#define vkd3d_memcpy_aligned_cached(dst, src, size) memcpy((uint8_t *)(dst), (const uint8_t *)(src), size)
#define vkd3d_memcpy_non_temporal_barrier() ((void)0)
#endif

#endif
