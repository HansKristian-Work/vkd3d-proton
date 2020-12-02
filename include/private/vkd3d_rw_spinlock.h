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

#ifndef __VKD3D_RW_SPINLOCK_H
#define __VKD3D_RW_SPINLOCK_H

#include "vkd3d_spinlock.h"

#define VKD3D_RW_SPINLOCK_WRITE 1u
#define VKD3D_RW_SPINLOCK_READ 2u
#define VKD3D_RW_SPINLOCK_IDLE 0u

static inline void rw_spinlock_acquire_read(spinlock_t *spinlock)
{
    uint32_t count = vkd3d_atomic_uint32_add(spinlock, VKD3D_RW_SPINLOCK_READ, vkd3d_memory_order_acquire);
    while (count & VKD3D_RW_SPINLOCK_WRITE)
    {
#ifdef __SSE2__
        _mm_pause();
#endif
        count = vkd3d_atomic_uint32_load_explicit(spinlock, vkd3d_memory_order_acquire);
    }
}

static inline void rw_spinlock_release_read(spinlock_t *spinlock)
{
    vkd3d_atomic_uint32_sub(spinlock, VKD3D_RW_SPINLOCK_READ, vkd3d_memory_order_release);
}

static inline void rw_spinlock_acquire_write(spinlock_t *spinlock)
{
    while (vkd3d_atomic_uint32_compare_exchange(spinlock,
            VKD3D_RW_SPINLOCK_IDLE, VKD3D_RW_SPINLOCK_WRITE,
            vkd3d_memory_order_acquire, vkd3d_memory_order_relaxed) != VKD3D_RW_SPINLOCK_IDLE)
    {
#ifdef __SSE2__
        _mm_pause();
#endif
    }
}

static inline void rw_spinlock_release_write(spinlock_t *spinlock)
{
    vkd3d_atomic_uint32_and(spinlock, ~VKD3D_RW_SPINLOCK_WRITE, vkd3d_memory_order_release);
}

#endif
