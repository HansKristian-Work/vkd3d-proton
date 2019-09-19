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

#include "config.h"

#if defined(HAVE_PTHREAD_H)
#include <pthread.h>

typedef struct vkd3d_pthread
{
    pthread_t thread;
} vkd3d_pthread_t;

typedef struct vkd3d_pthread_mutex
{
    pthread_mutex_t lock;
} vkd3d_pthread_mutex_t;

typedef struct vkd3d_pthread_cond
{
    pthread_cond_t cond;
} vkd3d_pthread_cond_t;

static inline int vkd3d_pthread_create(vkd3d_pthread_t *thread, void * (*thread_fun)(void *), void *arg)
{
    return pthread_create(&thread->thread, NULL, thread_fun, arg);
}

static inline int vkd3d_pthread_join(vkd3d_pthread_t thread)
{
    return pthread_join(thread.thread, NULL);
}

static inline int vkd3d_pthread_mutex_init(vkd3d_pthread_mutex_t *lock)
{
    return pthread_mutex_init(&lock->lock, NULL);
}

static inline int vkd3d_pthread_mutex_destroy(vkd3d_pthread_mutex_t *lock)
{
    return pthread_mutex_destroy(&lock->lock);
}

static inline int vkd3d_pthread_mutex_lock(vkd3d_pthread_mutex_t *lock)
{
    return pthread_mutex_lock(&lock->lock);
}

static inline int vkd3d_pthread_mutex_unlock(vkd3d_pthread_mutex_t *lock)
{
    return pthread_mutex_unlock(&lock->lock);
}

static inline int vkd3d_pthread_cond_init(vkd3d_pthread_cond_t *cond)
{
    return pthread_cond_init(&cond->cond, NULL);
}

static inline int vkd3d_pthread_cond_destroy(vkd3d_pthread_cond_t *cond)
{
    return pthread_cond_destroy(&cond->cond);
}

static inline int vkd3d_pthread_cond_signal(vkd3d_pthread_cond_t *cond)
{
    return pthread_cond_signal(&cond->cond);
}

static inline int vkd3d_pthread_cond_broadcast(vkd3d_pthread_cond_t *cond)
{
    return pthread_cond_broadcast(&cond->cond);
}

static inline int vkd3d_pthread_cond_wait(vkd3d_pthread_cond_t *cond, vkd3d_pthread_mutex_t *lock)
{
    return pthread_cond_wait(&cond->cond, &lock->lock);
}

#elif defined(_WIN32) /* HAVE_PTHREAD_H */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct vkd3d_pthread
{
    HANDLE thread;
    DWORD id;
} vkd3d_pthread_t;

typedef struct vkd3d_pthread_mutex
{
    CRITICAL_SECTION *lock;
} vkd3d_pthread_mutex_t;

typedef struct vkd3d_pthread_cond
{
    CONDITION_VARIABLE *cond;
} vkd3d_pthread_cond_t;

struct vkd3d_pthread_wrapper_struct
{
    void * (*routine)(void *);
    void *arg;
};

static DWORD WINAPI win32_thread_wrapper_routine(struct vkd3d_pthread_wrapper_struct *wrapper)
{
    wrapper->routine(wrapper->arg);
    vkd3d_free(wrapper);
    return 0;
}

static inline int vkd3d_pthread_create(vkd3d_pthread_t *thread, void * (*thread_fun)(void *), void *arg)
{
    struct vkd3d_pthread_wrapper_struct *wrapper = vkd3d_malloc(sizeof(*wrapper));
    wrapper->routine = thread_fun;
    wrapper->arg = arg;
    thread->thread = CreateThread(NULL, 0, win32_thread_wrapper_routine, wrapper, 0, &thread->id);
    return 0;
}

static inline int vkd3d_pthread_join(vkd3d_pthread_t thread)
{
    int success = WaitForSingleObject(thread.thread, INFINITE) == WAIT_OBJECT_0;
    CloseHandle(thread.thread);
    return success ? 0 : -1;
}

static inline int vkd3d_pthread_mutex_init(vkd3d_pthread_mutex_t *lock)
{
    lock->lock = vkd3d_malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(lock->lock);
    return 0;
}

static inline int vkd3d_pthread_mutex_lock(vkd3d_pthread_mutex_t *lock)
{
    EnterCriticalSection(lock->lock);
    return 0;
}

static inline int vkd3d_pthread_mutex_unlock(vkd3d_pthread_mutex_t *lock)
{
    LeaveCriticalSection(lock->lock);
    return 0;
}

static inline int vkd3d_pthread_mutex_destroy(vkd3d_pthread_mutex_t *lock)
{
    DeleteCriticalSection(lock->lock);
    vkd3d_free(lock->lock);
    return 0;
}

static inline int vkd3d_pthread_cond_init(vkd3d_pthread_cond_t *cond)
{
    cond->cond = vkd3d_malloc(sizeof(CONDITION_VARIABLE));
    InitializeConditionVariable(cond->cond);
    return 0;
}

static inline void vkd3d_pthread_cond_destroy(vkd3d_pthread_cond_t *cond)
{
    vkd3d_free(cond->cond);
}

static inline int vkd3d_pthread_cond_signal(vkd3d_pthread_cond_t *cond)
{
    WakeConditionVariable(cond->cond);
    return 0;
}

static inline int vkd3d_pthread_cond_broadcast(vkd3d_pthread_cond_t *cond)
{
    WakeAllConditionVariable(cond->cond);
    return 0;
}

static inline int vkd3d_pthread_cond_wait(vkd3d_pthread_cond_t *cond, vkd3d_pthread_mutex_t *lock)
{
    bool ret = SleepConditionVariableCS(cond->cond, lock->lock, INFINITE);
    return ret ? 0 : -1;
}

#else /* HAVE_PTHREAD_H */
#error "Threads are not supported. Cannot build."
#endif /* HAVE_PTHREAD_H */

static inline void vkd3d_set_thread_name(const char *name)
{
#if defined(_MSC_VER)
    (void)name;
#elif defined(HAVE_PTHREAD_SETNAME_NP_2)
    pthread_setname_np(pthread_self(), name);
#elif defined(HAVE_PTHREAD_SETNAME_NP_1)
    pthread_setname_np(name);
#else
    (void)name;
#endif
}

#endif /* __VKD3D_THREADS_H */
