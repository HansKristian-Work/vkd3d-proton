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
    vkd3d_memory_order_relaxed,
    vkd3d_memory_order_consume,
    vkd3d_memory_order_acquire,
    vkd3d_memory_order_release,
    vkd3d_memory_order_acq_rel,
    vkd3d_memory_order_seq_cst,
} vkd3d_memory_order;

# define vkd3d_atomic_rw_barrier() _ReadWriteBarrier()

FORCEINLINE void vkd3d_atomic_load_barrier(vkd3d_memory_order order)
{
    switch (order)
    {
        case vkd3d_memory_order_consume:
        case vkd3d_memory_order_acquire:
        case vkd3d_memory_order_seq_cst:
            vkd3d_atomic_rw_barrier();
            break;

        case vkd3d_memory_order_relaxed:
        default:
            break;
    }
}

// Redefinitions for invalid memory orders...
#define InterlockedExchangeRelease InterlockedExchange

#define vkd3d_atomic_choose_intrinsic(order, result, intrinsic, ...)                     \
    switch (order)                                                                       \
    {                                                                                    \
        case vkd3d_memory_order_relaxed: result = intrinsic##NoFence  (__VA_ARGS__); break; \
        case vkd3d_memory_order_consume:                                                    \
        case vkd3d_memory_order_acquire: result = intrinsic##Acquire (__VA_ARGS__); break;  \
        case vkd3d_memory_order_release: result = intrinsic##Release (__VA_ARGS__); break;  \
        case vkd3d_memory_order_acq_rel:                                                    \
        case vkd3d_memory_order_seq_cst: result = intrinsic          (__VA_ARGS__); break;  \
    }

FORCEINLINE uint32_t vkd3d_uint32_atomic_load_explicit(uint32_t *target, vkd3d_memory_order order)
{
    uint32_t value = *((volatile uint32_t*)target);
    vkd3d_atomic_load_barrier(order);
    return value;
}

FORCEINLINE void vkd3d_uint32_atomic_store_explicit(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    switch (order)
    {
        case vkd3d_memory_order_release: vkd3d_atomic_rw_barrier(); // fallthrough...
        case vkd3d_memory_order_relaxed: *((volatile uint32_t*)target) = value; break;
        default:
        case vkd3d_memory_order_seq_cst:
            (void) InterlockedExchange((LONG*) target, value);
    }
    
}

FORCEINLINE uint32_t vkd3d_uint32_atomic_exchange_explicit(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedExchange, (LONG*)target, value);
    return result;
}

#elif defined(__GNUC__) || defined(__clang__)

#define vkd3d_memory_order_relaxed __ATOMIC_RELAXED
#define vkd3d_memory_order_consume __ATOMIC_CONSUME
#define vkd3d_memory_order_acquire __ATOMIC_ACQUIRE
#define vkd3d_memory_order_release __ATOMIC_RELEASE
#define vkd3d_memory_order_acq_rel __ATOMIC_ACQ_REL
#define vkd3d_memory_order_seq_cst __ATOMIC_SEQ_CST 

# define vkd3d_uint32_atomic_load_explicit(target, order)            __atomic_load_n(target, order)
# define vkd3d_uint32_atomic_store_explicit(target, value, order)    __atomic_store_n(target, value, order)
# define vkd3d_uint32_atomic_exchange_explicit(target, value, order) __atomic_exchange_n(target, value, order)

# ifndef __MINGW32__
/* Unfortunately only fetch_add is in stdatomic
 * so use the common GCC extensions for these. */
#  define InterlockedIncrement(target) __atomic_add_fetch(target, 1, vkd3d_memory_order_seq_cst)
#  define InterlockedDecrement(target) __atomic_sub_fetch(target, 1, vkd3d_memory_order_seq_cst)
# endif

#else

# error "No atomics for this platform"

#endif

#endif