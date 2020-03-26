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

#if (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>

#elif defined(__GNUC__) && ((__GNUC__ * 1000 + __GNUC_MINOR__) >= 4007)

/* Can use GCC's intrinsic versions of C11 atomics. */
#define atomic_load_explicit(lock, order) __atomic_load_n(lock, order)
#define atomic_store_explicit(lock, value, order) __atomic_store_n(lock, value, order)
#define atomic_exchange_explicit(lock, value, order) __atomic_exchange_n(lock, value, order)
#define memory_order_relaxed __ATOMIC_RELAXED
#define memory_order_acquire __ATOMIC_ACQUIRE
#define memory_order_release __ATOMIC_RELEASE

#elif defined(__GNUC__)
/* Legacy GCC intrinsics */
#define atomic_try_lock(lock) !__sync_lock_test_and_set(lock, 1)
#define atomic_unlock(lock) __sync_lock_release(lock)

#elif defined(_MSC_VER)
#include <intrin.h>
#define atomic_try_lock(lock) !InterlockedExchange(lock, 1)
/* There is no "unlock" equivalent on MSVC, but exchange without consuming the result
 * is effectively an unlock with correct memory semantics.
 * Compiler is be free to optimize this. */
#define atomic_unlock(lock) InterlockedExchange(lock, 0)
#else
#error "No possible spinlock implementation for this platform."
#endif

/* Generic C11 implementations of try_lock and unlock. */
#ifndef atomic_try_lock
#define atomic_try_lock(lock) \
    (!atomic_load_explicit(lock, memory_order_relaxed) && \
     !atomic_exchange_explicit(lock, 1u, memory_order_acquire))
#endif

#ifndef atomic_unlock
#define atomic_unlock(lock) atomic_store_explicit(lock, 0u, memory_order_release)
#endif

#ifdef _MSC_VER
typedef LONG spinlock_t;
#else
typedef uint32_t spinlock_t;
#endif

static inline void spinlock_init(spinlock_t *lock)
{
    *lock = 0;
}

static inline bool spinlock_try_acquire(spinlock_t *lock)
{
    return atomic_try_lock(lock);
}

static inline void spinlock_acquire(spinlock_t *lock)
{
    while (!spinlock_try_acquire(lock))
        continue;
}

static inline void spinlock_release(spinlock_t *lock)
{
    atomic_unlock(lock);
}

#endif
