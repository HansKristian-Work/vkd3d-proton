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

/* Redefinitions for invalid memory orders */
#define InterlockedExchangeRelease     InterlockedExchange
#define InterlockedExchangeRelease64   InterlockedExchange64

#if defined(_M_IX86)
/* Some 64-bit atomics with specific memory orders don't exist on x86... */
#define InterlockedAndNoFence64 InterlockedAnd64
#define InterlockedAndAcquire64 InterlockedAnd64
#define InterlockedAndRelease64 InterlockedAnd64
#define InterlockedOrNoFence64 InterlockedOr64
#define InterlockedOrAcquire64 InterlockedOr64
#define InterlockedOrRelease64 InterlockedOr64

#else
/* ... and have inconsistent naming scheme on other targets. */
#define InterlockedAndNoFence64 InterlockedAnd64NoFence
#define InterlockedAndAcquire64 InterlockedAnd64Acquire
#define InterlockedAndRelease64 InterlockedAnd64Release
#define InterlockedOrNoFence64 InterlockedOr64NoFence
#define InterlockedOrAcquire64 InterlockedOr64Acquire
#define InterlockedOrRelease64 InterlockedOr64Release

#endif

#define vkd3d_atomic_choose_intrinsic(order, result, intrinsic, suffix, ...)                        \
    switch (order)                                                                                  \
    {                                                                                               \
        case vkd3d_memory_order_relaxed: result = intrinsic##NoFence##suffix  (__VA_ARGS__); break; \
        case vkd3d_memory_order_consume:                                                            \
        case vkd3d_memory_order_acquire: result = intrinsic##Acquire##suffix (__VA_ARGS__); break;  \
        case vkd3d_memory_order_release: result = intrinsic##Release##suffix (__VA_ARGS__); break;  \
        case vkd3d_memory_order_acq_rel:                                                            \
        case vkd3d_memory_order_seq_cst: result = intrinsic##suffix          (__VA_ARGS__); break;  \
    }

FORCEINLINE uint32_t vkd3d_atomic_uint32_load_explicit(uint32_t *target, vkd3d_memory_order order)
{
    uint32_t value = *((volatile uint32_t*)target);
    vkd3d_atomic_load_barrier(order);
    return value;
}

FORCEINLINE void vkd3d_atomic_uint32_store_explicit(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    switch (order)
    {
        case vkd3d_memory_order_release: vkd3d_atomic_rw_barrier(); /* fallthrough */
        case vkd3d_memory_order_relaxed: *((volatile uint32_t*)target) = value; break;
        default:
        case vkd3d_memory_order_seq_cst:
            (void) InterlockedExchange((LONG*) target, value);
    }
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_exchange_explicit(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedExchange, /* no suffix */,(LONG*)target, value);
    return result;
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_increment(uint32_t *target, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedIncrement, /* no suffix */,(LONG*)target);
    return result;
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_decrement(uint32_t *target, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedDecrement, /* no suffix */,(LONG*)target);
    return result;
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_add(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedAdd, /* no suffix */,(LONG*)target, value);
    return result;
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_sub(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedAdd, /* no suffix */,(LONG*)target, (uint32_t)(-(int32_t)value));
    return result;
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_and(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedAnd, /* no suffix */,(LONG*)target, value);
    return result;
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_or(uint32_t *target, uint32_t value, vkd3d_memory_order order)
{
    uint32_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedOr, /* no suffix */,(LONG*)target, value);
    return result;
}

FORCEINLINE uint32_t vkd3d_atomic_uint32_compare_exchange(uint32_t* target, uint32_t expected, uint32_t desired,
        vkd3d_memory_order success_order, vkd3d_memory_order fail_order)
{
    uint32_t result;
    /* InterlockedCompareExchange has desired (ExChange) first, then expected (Comperand) */
    vkd3d_atomic_choose_intrinsic(success_order, result, InterlockedCompareExchange, /* no suffix */, (LONG*)target, desired, expected);
    return result;
}

FORCEINLINE uint64_t vkd3d_atomic_uint64_load_explicit(UINT64 *target, vkd3d_memory_order order)
{
#if defined(_M_IX86)
    /* On 32-bit x86, only 32-bit aligned reads or smaller will not tear. lock cmpxchg8b reads all 8 bytes in
     * a single atomic operation. Compare and exchanges with a value of 0, so it will never change the value. */
    (void)order;
    return (uint64_t)InterlockedCompareExchange64((LONG64*)target, 0, 0);
#else
    uint64_t value = *((volatile uint64_t*)target);
    vkd3d_atomic_load_barrier(order);
    return value;
#endif
}

FORCEINLINE void vkd3d_atomic_uint64_store_explicit(UINT64 *target, uint64_t value, vkd3d_memory_order order)
{
#if defined(_M_IX86)
    /* On 32-bit x86, only 32-bit aligned writes or smaller will not tear. lock cmpxchg8b writes all 8 bytes in
     * a single atomic operation. */
    (void)order;
    (void) InterlockedExchange64((LONG64*) target, value);
#else
    switch (order)
    {
        case vkd3d_memory_order_release: vkd3d_atomic_rw_barrier(); /* fallthrough */
        case vkd3d_memory_order_relaxed: *((volatile uint64_t*)target) = value; break;
        default:
        case vkd3d_memory_order_seq_cst:
            (void) InterlockedExchange64((LONG64*) target, value);
    }
#endif
}

FORCEINLINE uint64_t vkd3d_atomic_uint64_exchange_explicit(UINT64 *target, uint64_t value, vkd3d_memory_order order)
{
    uint64_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedExchange, 64, (LONG64*)target, value);
    return result;
}

FORCEINLINE uint64_t vkd3d_atomic_uint64_increment(UINT64 *target, vkd3d_memory_order order)
{
    uint64_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedIncrement, 64, (LONG64*)target);
    return result;
}

FORCEINLINE uint64_t vkd3d_atomic_uint64_decrement(UINT64 *target, vkd3d_memory_order order)
{
    uint64_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedDecrement, 64, (LONG64*)target);
    return result;
}

FORCEINLINE uint64_t vkd3d_atomic_uint64_and(UINT64 *target, uint64_t value, vkd3d_memory_order order)
{
    uint64_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedAnd, 64, (LONG64*)target, value);
    return result;
}

FORCEINLINE uint64_t vkd3d_atomic_uint64_or(UINT64 *target, uint64_t value, vkd3d_memory_order order)
{
    uint64_t result;
    vkd3d_atomic_choose_intrinsic(order, result, InterlockedOr, 64, (LONG64*)target, value);
    return result;
}

FORCEINLINE uint64_t vkd3d_atomic_uint64_compare_exchange(UINT64* target, uint64_t expected, uint64_t desired,
        vkd3d_memory_order success_order, vkd3d_memory_order fail_order)
{
    uint64_t result;
    /* InterlockedCompareExchange has desired (ExChange) first, then expected (Comperand). Use UINT64 to mark 8-byte alignment. */
    vkd3d_atomic_choose_intrinsic(success_order, result, InterlockedCompareExchange, 64, (LONG64*)target, desired, expected);
    return result;
}

#elif defined(__GNUC__) || defined(__clang__)

typedef enum
{
    vkd3d_memory_order_relaxed = __ATOMIC_RELAXED,
    vkd3d_memory_order_consume = __ATOMIC_CONSUME,
    vkd3d_memory_order_acquire = __ATOMIC_ACQUIRE,
    vkd3d_memory_order_release = __ATOMIC_RELEASE,
    vkd3d_memory_order_acq_rel = __ATOMIC_ACQ_REL,
    vkd3d_memory_order_seq_cst = __ATOMIC_SEQ_CST,
} vkd3d_memory_order;

# define vkd3d_atomic_generic_load_explicit(target, order)            __atomic_load_n(target, order)
# define vkd3d_atomic_generic_store_explicit(target, value, order)    __atomic_store_n(target, value, order)
# define vkd3d_atomic_generic_exchange_explicit(target, value, order) __atomic_exchange_n(target, value, order)
# define vkd3d_atomic_generic_increment(target, order)                __atomic_add_fetch(target, 1, order)
# define vkd3d_atomic_generic_decrement(target, order)                __atomic_sub_fetch(target, 1, order)
# define vkd3d_atomic_generic_add(target, value, order)               __atomic_add_fetch(target, value, order)
# define vkd3d_atomic_generic_sub(target, value, order)               __atomic_sub_fetch(target, value, order)
# define vkd3d_atomic_generic_and(target, value, order)               __atomic_fetch_and(target, value, order)
# define vkd3d_atomic_generic_or(target, value, order)                __atomic_fetch_or(target, value, order)

# define vkd3d_atomic_uint32_load_explicit(target, order)            vkd3d_atomic_generic_load_explicit(target, order)
# define vkd3d_atomic_uint32_store_explicit(target, value, order)    vkd3d_atomic_generic_store_explicit(target, value, order)
# define vkd3d_atomic_uint32_exchange_explicit(target, value, order) vkd3d_atomic_generic_exchange_explicit(target, value, order)
# define vkd3d_atomic_uint32_increment(target, order)                vkd3d_atomic_generic_increment(target, order)
# define vkd3d_atomic_uint32_decrement(target, order)                vkd3d_atomic_generic_decrement(target, order)
# define vkd3d_atomic_uint32_add(target, value, order)               vkd3d_atomic_generic_add(target, value, order)
# define vkd3d_atomic_uint32_sub(target, value, order)               vkd3d_atomic_generic_sub(target, value, order)
# define vkd3d_atomic_uint32_and(target, value, order)               vkd3d_atomic_generic_and(target, value, order)
# define vkd3d_atomic_uint32_or(target, value, order)                vkd3d_atomic_generic_or(target, value, order)
static inline uint32_t vkd3d_atomic_uint32_compare_exchange(uint32_t* target, uint32_t expected, uint32_t desired,
        vkd3d_memory_order success_order, vkd3d_memory_order fail_order)
{
    /* Expected is written to with the old value in the case that *target != expected */
    __atomic_compare_exchange_n(target, &expected, desired, 0, success_order, fail_order);
    return expected;
}

# define vkd3d_atomic_uint64_load_explicit(target, order)            vkd3d_atomic_generic_load_explicit(target, order)
# define vkd3d_atomic_uint64_store_explicit(target, value, order)    vkd3d_atomic_generic_store_explicit(target, value, order)
# define vkd3d_atomic_uint64_exchange_explicit(target, value, order) vkd3d_atomic_generic_exchange_explicit(target, value, order)
# define vkd3d_atomic_uint64_increment(target, order)                vkd3d_atomic_generic_increment(target, order)
# define vkd3d_atomic_uint64_decrement(target, order)                vkd3d_atomic_generic_decrement(target, order)
# define vkd3d_atomic_uint64_and(target, value, order)               vkd3d_atomic_generic_and(target, value, order)
# define vkd3d_atomic_uint64_or(target, value, order)                vkd3d_atomic_generic_or(target, value, order)
static inline uint64_t vkd3d_atomic_uint64_compare_exchange(UINT64* target, uint64_t expected, uint64_t desired,
        vkd3d_memory_order success_order, vkd3d_memory_order fail_order)
{
    /* Expected is written to with the old value in the case that *target != expected. Use UINT64 to mark 8-byte alignment. */
    __atomic_compare_exchange_n(target, &expected, desired, 0, success_order, fail_order);
    return expected;
}

# ifndef __MINGW32__
#  define InterlockedIncrement(target)                            vkd3d_atomic_uint32_increment(target, vkd3d_memory_order_seq_cst)
#  define InterlockedDecrement(target)                            vkd3d_atomic_uint32_decrement(target, vkd3d_memory_order_seq_cst)
#  define InterlockedCompareExchange(target, desired, expected)   vkd3d_atomic_uint32_compare_exchange(target, expected, desired, vkd3d_memory_order_seq_cst, vkd3d_memory_order_acquire)

#  define InterlockedIncrement64(target)                          vkd3d_atomic_uint64_increment(target, vkd3d_memory_order_seq_cst)
#  define InterlockedDecrement64(target)                          vkd3d_atomic_uint64_decrement(target, vkd3d_memory_order_seq_cst)
#  define InterlockedCompareExchange64(target, desired, expected) vkd3d_atomic_uint64_compare_exchange(target, expected, desired, vkd3d_memory_order_seq_cst, vkd3d_memory_order_acquire)
# endif

#else

# error "No atomics for this platform"

#endif

#if INTPTR_MAX == INT64_MAX
# define vkd3d_atomic_ptr_load_explicit(target, order)                       ((void *)vkd3d_atomic_uint64_load_explicit((UINT64 *)target, order))
# define vkd3d_atomic_ptr_store_explicit(target, value, order)               (vkd3d_atomic_uint64_store_explicit((UINT64 *)target, (uint64_t)value, order))
# define vkd3d_atomic_ptr_exchange_explicit(target, value, order)            ((void *)vkd3d_atomic_uint64_exchange_explicit((UINT64 *)target, (uint64_t)value, order))
# define vkd3d_atomic_ptr_increment(target, order)                           ((void *)vkd3d_atomic_uint64_increment((UINT64 *)target, order))
# define vkd3d_atomic_ptr_decrement(target, order)                           ((void *)vkd3d_atomic_uint64_decrement((UINT64 *)target, order))
# define vkd3d_atomic_ptr_compare_exchange(target, expected, desired, success_order, fail_order) \
        ((void *)vkd3d_atomic_uint64_compare_exchange((UINT64 *)target, (uint64_t)expected, (uint64_t)desired, success_order, fail_order))
#else
# define vkd3d_atomic_ptr_load_explicit(target, order)                       ((void *)vkd3d_atomic_uint32_load_explicit((uint32_t *)target, order))
# define vkd3d_atomic_ptr_store_explicit(target, value, order)               (vkd3d_atomic_uint32_store_explicit((uint32_t *)target, (uint32_t)value, order))
# define vkd3d_atomic_ptr_exchange_explicit(target, value, order)            ((void *)vkd3d_atomic_uint32_exchange_explicit((uint32_t *)target, (uint32_t)value, order))
# define vkd3d_atomic_ptr_increment(target, order)                           ((void *)vkd3d_atomic_uint32_increment((uint32_t *)target, order))
# define vkd3d_atomic_ptr_decrement(target, order)                           ((void *)vkd3d_atomic_uint32_decrement((uint32_t *)target, order))
# define vkd3d_atomic_ptr_compare_exchange(target, expected, desired, success_order, fail_order) \
        ((void *)vkd3d_atomic_uint32_compare_exchange((uint32_t *)target, (uint32_t)expected, (uint32_t)desired, success_order, fail_order))
#endif

#endif
