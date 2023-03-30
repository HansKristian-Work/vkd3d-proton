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
#include "vkd3d_common.h"

#ifdef VKD3D_ENABLE_PROFILING

#ifdef TRACY_ENABLE

#include <TracyC.h>

void tracy_set_thread_name( const char* name );
void tracy_emit_frame_mark();
TracyCZoneCtx tracy_emit_zone_begin( const struct ___tracy_source_location_data* srcloc, int active );
void tracy_emit_zone_end( TracyCZoneCtx ctx );

#define VKD3D_PROFILE_THREAD_NAME(name) tracy_set_thread_name((name))
#define VKD3D_PROFILE_FRAME() tracy_emit_frame_mark()

void vkd3d_init_profiling(void);
bool vkd3d_uses_profiling(void);

#define VKD3D_STRINGIZE1(x) #x
#define VKD3D_STRINGIZE(x) VKD3D_STRINGIZE1(x)

#define VKD3D_REGION_DECL(name) TracyCZoneCtx _vkd3d_tracy_##name
/* Copied from definition of TracyCZoneN. In this implementation, _vkd3d_tracy_##name is expected
 * to be defined by VKD3D_REGION_DECL. */
#define VKD3D_REGION_BEGIN(name) static const struct ___tracy_source_location_data TracyConcat(__tracy_source_location,__LINE__) = { VKD3D_STRINGIZE(name), __func__,  __FILE__, (uint32_t)__LINE__, 0 }; _vkd3d_tracy_##name = tracy_emit_zone_begin( &TracyConcat(__tracy_source_location,__LINE__), 1 )
#define VKD3D_REGION_END_ITERATIONS(name, iter) tracy_emit_zone_end(_vkd3d_tracy_##name)

#else

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
        _vkd3d_region_begin_tick_##name = vkd3d_get_current_time_ticks(); \
    } while(0)

#define VKD3D_REGION_END_ITERATIONS(name, iter) \
    do { \
        _vkd3d_region_end_tick_##name = vkd3d_get_current_time_ticks(); \
        vkd3d_profiling_notify_work(_vkd3d_region_index_##name, _vkd3d_region_begin_tick_##name, _vkd3d_region_end_tick_##name, iter); \
    } while(0)


#define VKD3D_PROFILE_THREAD_NAME(name) ((void)0)
#define VKD3D_PROFILE_FRAME() ((void)0)

#endif /* TRACY_ENABLE */

#else
static inline void vkd3d_init_profiling(void)
{
}
#define VKD3D_REGION_DECL(name) ((void)0)
#define VKD3D_REGION_BEGIN(name) ((void)0)
#define VKD3D_REGION_END_ITERATIONS(name, iter) ((void)0)
#define VKD3D_PROFILE_THREAD_NAME(name) ((void)0)
#define VKD3D_PROFILE_FRAME() ((void)0)
#endif /* VKD3D_ENABLE_PROFILING */

#define VKD3D_REGION_END(name) VKD3D_REGION_END_ITERATIONS(name, 1)

#endif /* __VKD3D_PROFILING_H */
