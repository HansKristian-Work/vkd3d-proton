/*
 * Copyright 2019 Hans-Kristian Arntzen for Valve
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

#ifndef __VKD3D_THREADS_H
#define __VKD3D_THREADS_H

#include "vkd3d_memory.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* pthread_t is passed by value in some functions,
 * which implies we need pthread_t to be a pointer type here. */
struct pthread
{
    HANDLE thread;
    DWORD id;
    void * (*routine)(void *);
    void *arg;
};
typedef struct pthread *pthread_t;

/* pthread_mutex_t is not copyable, so embed CS inline. */
typedef struct pthread_mutex
{
    SRWLOCK lock;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER {SRWLOCK_INIT}

/* pthread_cond_t is not copyable, so embed CV inline. */
typedef struct pthread_cond
{
    CONDITION_VARIABLE cond;
} pthread_cond_t;

typedef pthread_cond_t condvar_reltime_t;

static DWORD WINAPI win32_thread_wrapper_routine(void *arg)
{
    pthread_t thread = arg;
    thread->routine(thread->arg);
    return 0;
}

static inline int pthread_create(pthread_t *out_thread, void *attr, void * (*thread_fun)(void *), void *arg)
{
    pthread_t thread = vkd3d_calloc(1, sizeof(*thread));
    if (!thread)
        return -1;

    (void)attr;
    thread->routine = thread_fun;
    thread->arg = arg;
    thread->thread = CreateThread(NULL, 0, win32_thread_wrapper_routine, thread, 0, &thread->id);
    if (!thread->thread)
    {
        vkd3d_free(thread);
        return -1;
    }
    *out_thread = thread;
    return 0;
}

static inline int pthread_join(pthread_t thread, void **ret)
{
    int success;
    (void)ret;
    success = WaitForSingleObject(thread->thread, INFINITE) == WAIT_OBJECT_0;
    if (success)
    {
        CloseHandle(thread->thread);
        vkd3d_free(thread);
    }
    return success ? 0 : -1;
}

static inline int pthread_mutex_init(pthread_mutex_t *lock, void *attr)
{
    (void)attr;
    InitializeSRWLock(&lock->lock);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *lock)
{
    AcquireSRWLockExclusive(&lock->lock);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *lock)
{
    ReleaseSRWLockExclusive(&lock->lock);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *lock)
{
    return 0;
}

/* SRWLocks distinguish between write and read unlocks, but pthread interface does not,
 * so make a trivial wrapper type instead to avoid any possible API conflicts. */
typedef struct rwlock
{
    SRWLOCK rwlock;
} rwlock_t;

static inline int rwlock_init(rwlock_t *lock)
{
    InitializeSRWLock(&lock->rwlock);
    return 0;
}

static inline int rwlock_lock_write(rwlock_t *lock)
{
    AcquireSRWLockExclusive(&lock->rwlock);
    return 0;
}

static inline int rwlock_lock_read(rwlock_t *lock)
{
    AcquireSRWLockShared(&lock->rwlock);
    return 0;
}

static inline int rwlock_unlock_write(rwlock_t *lock)
{
    ReleaseSRWLockExclusive(&lock->rwlock);
    return 0;
}

static inline int rwlock_unlock_read(rwlock_t *lock)
{
    ReleaseSRWLockShared(&lock->rwlock);
    return 0;
}

static inline int rwlock_destroy(rwlock_t *lock)
{
    return 0;
}

static inline int pthread_cond_init(pthread_cond_t *cond, void *attr)
{
    (void)attr;
    InitializeConditionVariable(&cond->cond);
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *cond)
{
    WakeConditionVariable(&cond->cond);
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *cond)
{
    WakeAllConditionVariable(&cond->cond);
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *lock)
{
    BOOL ret = SleepConditionVariableSRW(&cond->cond, &lock->lock, INFINITE, 0);
    return ret ? 0 : -1;
}

static inline int condvar_reltime_init(condvar_reltime_t *cond)
{
    return pthread_cond_init(cond, NULL);
}

static inline int condvar_reltime_destroy(condvar_reltime_t *cond)
{
    return pthread_cond_destroy(cond);
}

static inline int condvar_reltime_signal(condvar_reltime_t *cond)
{
    return pthread_cond_signal(cond);
}

static inline int condvar_reltime_wait_timeout_seconds(condvar_reltime_t *cond, pthread_mutex_t *lock, unsigned int seconds)
{
    BOOL ret = SleepConditionVariableSRW(&cond->cond, &lock->lock, seconds * 1000, 0);
    if (ret)
        return 0;
    else if (GetLastError() == ERROR_TIMEOUT)
        return 1;
    else
        return -1;
}

static inline void vkd3d_set_thread_name(const char *name)
{
    (void)name;
}

typedef INIT_ONCE pthread_once_t;
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT

static inline BOOL CALLBACK pthread_once_wrapper(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    void (*func)(void) = parameter;
    (void)once;
    (void)context;
    func();
    return TRUE;
}

static inline void pthread_once(pthread_once_t *once, void (*func)(void))
{
    InitOnceExecuteOnce(once, pthread_once_wrapper, func, NULL);
}
#else
#include <pthread.h>
#include <errno.h>
#include <time.h>

static inline void vkd3d_set_thread_name(const char *name)
{
    pthread_setname_np(pthread_self(), name);
}

typedef struct rwlock
{
    pthread_rwlock_t rwlock;
} rwlock_t;

static inline int rwlock_init(rwlock_t *lock)
{
    return pthread_rwlock_init(&lock->rwlock, NULL);
}

static inline int rwlock_lock_write(rwlock_t *lock)
{
    return pthread_rwlock_wrlock(&lock->rwlock);
}

static inline int rwlock_lock_read(rwlock_t *lock)
{
    return pthread_rwlock_rdlock(&lock->rwlock);
}

static inline int rwlock_unlock_write(rwlock_t *lock)
{
    return pthread_rwlock_unlock(&lock->rwlock);
}

static inline int rwlock_unlock_read(rwlock_t *lock)
{
    return pthread_rwlock_unlock(&lock->rwlock);
}

static inline int rwlock_destroy(rwlock_t *lock)
{
    return pthread_rwlock_destroy(&lock->rwlock);
}

typedef struct condvar_reltime
{
    pthread_cond_t cond;
} condvar_reltime_t;

static inline int condvar_reltime_init(condvar_reltime_t *cond)
{
    pthread_condattr_t attr;
    int rc;

    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    rc = pthread_cond_init(&cond->cond, &attr);
    pthread_condattr_destroy(&attr);

    return rc;
}

static inline void condvar_reltime_destroy(condvar_reltime_t *cond)
{
    pthread_cond_destroy(&cond->cond);
}

static inline int condvar_reltime_signal(condvar_reltime_t *cond)
{
    return pthread_cond_signal(&cond->cond);
}

static inline int condvar_reltime_wait_timeout_seconds(condvar_reltime_t *cond, pthread_mutex_t *lock, unsigned int seconds)
{
    struct timespec ts;
    int rc;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += seconds;

    /* This is absolute time. */
    rc = pthread_cond_timedwait(&cond->cond, lock, &ts);

    if (rc == ETIMEDOUT)
        return 1;
    else if (rc == 0)
        return 0;
    else
        return -1;
}

#define PTHREAD_ONCE_CALLBACK
#endif

#ifdef __linux__
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

static inline unsigned int vkd3d_get_current_thread_id(void)
{
#ifdef _WIN32
    return GetCurrentThreadId();
#elif defined(__linux__)
    return syscall(__NR_gettid);
#else
    return 0;
#endif
}

#endif /* __VKD3D_THREADS_H */
