/*
 * Copyright 2020 Philip Rebohle for Valve Corporation
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

#ifndef __VKD3D_SPINLOCK_H
#define __VKD3D_SPINLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "vkd3d_atomic.h"

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#define vkd3d_spinlock_try_lock(lock) \
    (!vkd3d_uint32_atomic_load_explicit(lock, memory_order_relaxed) && \
     !vkd3d_uint32_atomic_exchange_explicit(lock, 1u, memory_order_acquire))

#define vkd3d_spinlock_unlock(lock) vkd3d_uint32_atomic_store_explicit(lock, 0u, memory_order_release)

typedef _Atomic uint32_t spinlock_t;

static inline void spinlock_init(spinlock_t *lock)
{
    *lock = 0;
}

static inline bool spinlock_try_acquire(spinlock_t *lock)
{
    return vkd3d_spinlock_try_lock(lock);
}

static inline void spinlock_acquire(spinlock_t *lock)
{
    while (!spinlock_try_acquire(lock))
#ifdef __SSE2__
        _mm_pause();
#else
        continue;
#endif
}

static inline void spinlock_release(spinlock_t *lock)
{
    vkd3d_spinlock_unlock(lock);
}

#endif
