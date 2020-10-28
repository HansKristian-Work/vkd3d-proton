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

/* pthread_cond_t is not copyable, so embed CV inline. */
typedef struct pthread_cond
{
    CONDITION_VARIABLE cond;
} pthread_cond_t;

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
static inline void vkd3d_set_thread_name(const char *name)
{
    pthread_setname_np(pthread_self(), name);
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
