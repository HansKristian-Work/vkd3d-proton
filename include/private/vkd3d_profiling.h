/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_PROFILING_H
#define __VKD3D_PROFILING_H

#include "vkd3d_windows.h"
#include "vkd3d_spinlock.h"
#include <stdint.h>

#ifdef VKD3D_ENABLE_PROFILING

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

void vkd3d_init_profiling(void);
bool vkd3d_uses_profiling(void);
unsigned int vkd3d_profiling_register_region(const char *name, spinlock_t *lock, uint32_t *latch);
void vkd3d_profiling_notify_work(unsigned int index, uint64_t start_ticks, uint64_t end_ticks, unsigned int iteration_count);

static inline uint64_t vkd3d_profiling_get_tick_count(void)
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

#define VKD3D_REGION_DECL(name) \
    static uint32_t _vkd3d_region_latch_##name; \
    static spinlock_t _vkd3d_region_lock_##name; \
    uint64_t _vkd3d_region_begin_tick_##name; \
    uint64_t _vkd3d_region_end_tick_##name; \
    unsigned int _vkd3d_region_index_##name

#define VKD3D_REGION_BEGIN(name) \
    do { \
        if (!(_vkd3d_region_index_##name = vkd3d_atomic_uint32_load_explicit(&_vkd3d_region_latch_##name, vkd3d_memory_order_acquire))) \
            _vkd3d_region_index_##name = vkd3d_profiling_register_region(#name, &_vkd3d_region_lock_##name, &_vkd3d_region_latch_##name); \
        _vkd3d_region_begin_tick_##name = vkd3d_profiling_get_tick_count(); \
    } while(0)

#define VKD3D_REGION_END_ITERATIONS(name, iter) \
    do { \
        _vkd3d_region_end_tick_##name = vkd3d_profiling_get_tick_count(); \
        vkd3d_profiling_notify_work(_vkd3d_region_index_##name, _vkd3d_region_begin_tick_##name, _vkd3d_region_end_tick_##name, iter); \
    } while(0)

#else
static inline void vkd3d_init_profiling(void)
{
}
#define VKD3D_REGION_DECL(name) ((void)0)
#define VKD3D_REGION_BEGIN(name) ((void)0)
#define VKD3D_REGION_END_ITERATIONS(name, iter) ((void)0)
#endif /* VKD3D_ENABLE_PROFILING */

#define VKD3D_REGION_END(name) VKD3D_REGION_END_ITERATIONS(name, 1)

#endif /* __VKD3D_PROFILING_H */
