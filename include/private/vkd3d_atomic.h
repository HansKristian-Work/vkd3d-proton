/*
 * Copyright 2020 Joshua Ashton for Valve Corporation
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

#ifndef __VKD3D_ATOMIC_H
#define __VKD3D_ATOMIC_H

#include <stdint.h>

#if defined(_MSC_VER)

# include <intrin.h>

typedef enum
{
    memory_order_relaxed,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
} memory_order;

# define vkd3d_atomic_rw_barrier() _ReadWriteBarrier()
# define vkd3d_atomic_rw_barrier_acquire(order) { if (order == memory_order_acquire || order == memory_order_acq_rel) { vkd3d_atomic_rw_barrier(); } }
# define vkd3d_atomic_rw_barrier_release(order) { if (order == memory_order_release || order == memory_order_acq_rel) { vkd3d_atomic_rw_barrier(); } }

FORCEINLINE uint32_t vkd3d_uint32_atomic_load_explicit(uint32_t *target, memory_order order)
{
    uint32_t value = *((volatile uint32_t*)target);
    vkd3d_atomic_rw_barrier_acquire(order);
    return value;
}

FORCEINLINE void vkd3d_uint32_atomic_store_explicit(uint32_t *target, uint32_t value, memory_order order)
{
    vkd3d_atomic_rw_barrier_release(order);
    *((volatile uint32_t*)target) = value;
}

FORCEINLINE uint32_t vkd3d_uint32_atomic_exchange_explicit(uint32_t *target, uint32_t value, memory_order order)
{
    vkd3d_atomic_rw_barrier_release(order);
    uint32_t oldValue = InterlockedExchange((LONG*)target, value);
    vkd3d_atomic_rw_barrier_acquire(order);
    return oldValue;
}

#elif defined(__GNUC__) || defined(__clang__)

# include <stdatomic.h>

# define vkd3d_uint32_atomic_load_explicit(target, order)            atomic_load_explicit(target, order)
# define vkd3d_uint32_atomic_store_explicit(target, value, order)    atomic_store_explicit(target, value, order)
# define vkd3d_uint32_atomic_exchange_explicit(target, value, order) atomic_exchange_explicit(target, value, order)

# ifndef __MINGW32__
/* Unfortunately only fetch_add is in stdatomic
 * so use the common GCC extensions for these. */
#  define InterlockedIncrement(target) __atomic_add_fetch(target, 1, memory_order_seq_cst)
#  define InterlockedDecrement(target) __atomic_sub_fetch(target, 1, memory_order_seq_cst)
# endif

#else

# error "No atomics for this platform"

#endif

#endif