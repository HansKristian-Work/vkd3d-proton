/*
 * Copyright 2026 Hans-Kristian Arntzen for Valve Corporation
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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_shared_fence_syncfd.h"
#include "vkd3d_memory.h"
#include "vkd3d_threads.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <unistd.h>

#define KMT_DEVICE_MAX_PENDING_EDGES 1024
#define KMT_DEVICE_MAX_PENDING_SIGNALS 1024

struct kmt_epoll_data
{
    uint64_t order;
    int fd;
};

struct kmt_pending_edge
{
    uint64_t edge;
    uint64_t value;
    /* Only one of these can be >= 0.
     * For cross-process, need FD flinging to request real FD on demand.
     * For prototype purposes, we don't need this. */
    int syncfd;
    int eventfd;
};

struct kmt_pending_signal
{
    uint64_t fence_id;
    uint64_t order;
    uint64_t value;
    int syncfd;
};

/* Intended to be shared as shmem across processes.
 * For demonstration, implement as plain memory. */
struct kmt_device_shmem_data
{
    /* Global atomic. Used to generate IDs.
     * Also used to generate signal ordering. */
    UINT64 order;

    /* These can overflow.
     * Spamming too many event signals is allowed to return E_OUTOFMEMORY.
     * If there is no room to store pending signals we can block in signal
     * functions until there is room. Pending signals must complete in finite time (or we have device lost).
     */
    struct kmt_pending_signal pending_signal[KMT_DEVICE_MAX_PENDING_SIGNALS];
    uint32_t pending_signal_count;

    /* For cross-process, replace with futex based lock/condvar implementation.
     * We need to support cross process 32-bit and 64-bit sync, so we cannot use the easier APIs. */
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

/* This represents the payload exposed by creating a shared handle. */
struct kmt_fence_shmem_data
{
    /* Can be read atomically with acquire semantics. */
    uint64_t current_value;

    /* Must not be modified. */
    uint64_t id;

    /* For cross-process, replace with futex based lock/condvar implementation.
     * We need to support cross process 32-bit and 64-bit sync, so we cannot use the easier APIs.
     * Used to ensure that value and events are signaled atomically. */
    pthread_mutex_t lock;
    pthread_cond_t cond;

    struct kmt_pending_edge pending_edges[KMT_DEVICE_MAX_PENDING_EDGES];
    uint32_t pending_edges_count;

    /* TODO: We might want a refcount here to track when global handle refcounts reach 0 somehow? */
};

struct kmt_device_opaque
{
    int epoll_fd;
    int wake_fd;
    int poll_fd;
    pthread_t epoll_thread;

    /* Owns the blocks for shared fences.
     * With server based architecture, this lock won't be needed.
     */
    pthread_mutex_t fence_lock;
    struct kmt_fence_shmem_data **fences;
    size_t fences_size;
    size_t fences_count;

    struct kmt_device_shmem_data *shmem;
};

struct kmt_handle_opaque
{
    struct kmt_fence_shmem_data *shmem;
};

static int wake_buffer_sort_cb(const void *a_, const void *b_)
{
    const struct kmt_epoll_data *a = a_;
    const struct kmt_epoll_data *b = b_;
    if (a->order < b->order)
        return -1;
    else if (a->order > b->order)
        return 1;
    else
        return 0;
}

static struct kmt_pending_signal *kmt_device_find_pending_signal_locked(kmt_device device, uint64_t order)
{
    struct kmt_device_shmem_data *shmem = device->shmem;
    size_t i;

    for (i = 0; i < shmem->pending_signal_count; i++)
        if (shmem->pending_signal[i].order == order)
            return &shmem->pending_signal[i];

    return NULL;
}

static struct kmt_fence_shmem_data *kmt_device_find_fence_shmem_locked(kmt_device device, uint64_t fence_id)
{
    struct kmt_fence_shmem_data *shmem = NULL;
    size_t i = 0;

    for (i = 0; i < device->fences_count; i++)
    {
        if (device->fences[i]->id == fence_id)
        {
            shmem = device->fences[i];
            break;
        }
    }
    return shmem;
}

static void kmt_device_signal_fence_immediate_locked(kmt_device device, struct kmt_fence_shmem_data *fence, uint64_t value)
{
    bool completed_edge = false;
    size_t i = 0;

    fence->current_value = value;

    while (i < fence->pending_edges_count)
    {
        struct kmt_pending_edge *edge = &fence->pending_edges[i];
        if (value >= edge->value)
        {
            /* Signal an eventfd, (or ntsync, or whatever primitive we need).
             * For signal ordering reasons, this must happen after we update the current value.
             * If this signal is in response to ID3D12Fence::Signal(),
             * this eventfd might be cross-process, in which case we don't have the FD to signal.
             * We'll have to send a signal request to the server process here instead.
             * Most likely, we'll detect possible cross-process signals,
             * and if required, send an RPC request to signal events instead.
             * The server process will always have a copy of all FDs.
             */
            assert(edge->eventfd != 0);
            if (edge->eventfd >= 0)
            {
                const uint64_t sig = 1;
                write(edge->eventfd, &sig, sizeof(sig));

                /* For cross-process, we should probably just close this right away.
                 * This is an in-process event here, and we don't dup(), so ... */
            }

            /* If we materialize the wait, return -1 fd later.
             * This is supported in Vulkan import. */
            if (edge->syncfd >= 0)
            {
                close(edge->syncfd);
                edge->syncfd = -1;
            }

            *edge = fence->pending_edges[--fence->pending_edges_count];
            completed_edge = true;
        }
        else
        {
            i++;
        }
    }

    if (completed_edge)
        pthread_cond_broadcast(&fence->cond);
}

static void kmt_device_complete_fence(kmt_device device, uint64_t order)
{
    struct kmt_device_shmem_data *shmem = device->shmem;
    struct kmt_fence_shmem_data *fence;
    struct kmt_pending_signal *signal;
    uint64_t signal_value;
    uint64_t fence_id;
    bool wakeup;

    pthread_mutex_lock(&shmem->lock);
    {
        signal = kmt_device_find_pending_signal_locked(device, order);
        assert(signal);

        /* We might have a thread waiting for space in the pending signal list. */
        wakeup = shmem->pending_signal_count == KMT_DEVICE_MAX_PENDING_SIGNALS;

        signal_value = signal->value;
        fence_id = signal->fence_id;

        if (signal->syncfd >= 0)
            close(signal->syncfd);

        *signal = shmem->pending_signal[--shmem->pending_signal_count];

        if (wakeup)
            pthread_cond_broadcast(&shmem->cond);
    }
    pthread_mutex_unlock(&shmem->lock);

    /* We drop the lock here, so it's possible that wait_materialization will not see that
     * there was a materializing signal in flight, then go to sleep waiting for materialization.
     * Even if it's a little overzealous, we need to wake the cond variable when signaling here,
     * so the materialization thread can observe that the signal is in-fact complete. */

    /* This fence is not going to be removed by another thread.
     * This is only called from the epoll thread which is the only thread
     * that can add or remove fences.
     * However, for this server-less prototype that's not the case, so take and hold the fence lock.
     */
    pthread_mutex_unlock(&device->fence_lock);
    {
        fence = kmt_device_find_fence_shmem_locked(device, fence_id);
        if (fence)
        {
            pthread_mutex_lock(&fence->lock);
            kmt_device_signal_fence_immediate_locked(device, fence, signal_value);
            pthread_mutex_unlock(&fence->lock);
        }
    }
    pthread_mutex_unlock(&device->fence_lock);
}

void kmt_device_signal_fence_immediate(kmt_device device, kmt_handle fence, uint64_t value)
{
    struct kmt_fence_shmem_data *shmem = fence->shmem;
    pthread_mutex_lock(&shmem->lock);
    kmt_device_signal_fence_immediate_locked(device, fence->shmem, value);
    pthread_mutex_unlock(&shmem->lock);
}

bool kmt_device_register_sync_file(kmt_device device, kmt_handle fence, int fd, uint64_t value)
{
    struct kmt_fence_shmem_data *fence_shmem = fence->shmem;
    struct kmt_device_shmem_data *shmem = device->shmem;
    struct kmt_pending_signal *pending_signal;
    bool has_materialization = false;
    uint64_t order;
    size_t i;

    if (fd < 0)
        FIXME("Is it actually possible to get -1 fd?\n");

    /* Register the signal on the device. All signals are ordered with each other. */
    pthread_mutex_lock(&shmem->lock);
    {
        /* This should complete in finite time, or we have a device lost situation.
         * Could add timeouts here to check for that if need be. */
        while (shmem->pending_signal_count == KMT_DEVICE_MAX_PENDING_SIGNALS)
            pthread_cond_wait(&shmem->cond, &shmem->lock);

        order = vkd3d_atomic_uint64_increment(&device->shmem->order, vkd3d_memory_order_relaxed);

        pending_signal = &shmem->pending_signal[shmem->pending_signal_count++];
        pending_signal->order = order;
        pending_signal->fence_id = fence_shmem->id;
        pending_signal->value = value;
        pending_signal->syncfd = fd;
    }
    pthread_mutex_unlock(&shmem->lock);

    /* Threads waiting for materialization can now be unblocked. */
    pthread_mutex_lock(&fence_shmem->lock);
    {
        for (i = 0; i < fence_shmem->pending_edges_count; i++)
        {
            struct kmt_pending_edge *edge = &fence_shmem->pending_edges[i];
            if (value >= edge->value && edge->eventfd < 0 && edge->syncfd < 0)
            {
                /* Can now unblock a thread waiting for materialization.
                 * Use the first possible materialization. */
                edge->syncfd = dup(fd);
                has_materialization = true;
            }
        }

        if (has_materialization)
            pthread_cond_broadcast(&fence_shmem->cond);
    }
    pthread_mutex_unlock(&fence_shmem->lock);

    /* Server side only. For a multi-process setup we would send FD to server with order number. */
    {
        struct kmt_epoll_data *data;
        struct epoll_event ev = {0};
        int ret;

        data = vkd3d_calloc(1, sizeof(*data));
        data->order = order;
        data->fd = fd;
        ev.data.ptr = data;

        ev.events = EPOLLIN;
        ret = epoll_ctl(device->epoll_fd, EPOLL_CTL_ADD, fd, &ev);

        if (ret < 0)
        {
            vkd3d_free(data);
            /* FIXME: Deal with errors more gracefully here. */
            return false;
        }
    }

    return true;
}

static void *kmt_device_epoll_main(void *opaque_data)
{
    struct kmt_epoll_data *wake_buffer = NULL;
    struct epoll_event *events = NULL;
    kmt_device device = opaque_data;
    size_t wake_buffer_size = 0;
    int wake_buffer_count = 0;
    size_t events_size = 0;
    bool alive = true;
    int i;

    vkd3d_set_thread_name("epoll");
    vkd3d_array_reserve((void **)&events, &events_size, 64, sizeof(*events));

    /* All waits for sync_file are contained to this event loop.
     * This guarantees global signal ordering.
     */

    while (alive)
    {
        int ret = epoll_wait(device->epoll_fd, events, events_size, -1);

        /* Spurious wakeup. */
        if (ret == -1 && errno == EINTR)
            continue;

        if (ret <= 0)
            break;

        if ((size_t)ret == events_size)
        {
            /* We might have lost some events. For signal ordering, that's not acceptable.
             * Bump the size and try again. */
            vkd3d_array_reserve((void **)&events, &events_size, events_size * 2, sizeof(*events));
            continue;
        }

        vkd3d_array_reserve((void **)&wake_buffer, &wake_buffer_size, ret, sizeof(*wake_buffer));
        wake_buffer_count = 0;

        for (i = 0; i < ret; i++)
        {
            if (!events[i].data.ptr)
            {
                struct kmt_epoll_data data = {};
                while (read(device->poll_fd, &data, sizeof(data)) == sizeof(data))
                {
                    if (data.order == 0)
                        alive = false;
                    else
                        wake_buffer[wake_buffer_count++] = data;
                }
            }
            else
            {
                struct kmt_epoll_data *ptr = events[i].data.ptr;
                wake_buffer[wake_buffer_count++] = *ptr;
                vkd3d_free(ptr);
            }
        }

        /* Ensure signal order. If a signal was registered before another, and both events are signaled,
         * we must preserve the global ordering. */
        qsort(wake_buffer, wake_buffer_count, sizeof(*wake_buffer), wake_buffer_sort_cb);

        for (i = 0; i < wake_buffer_count; i++)
        {
            const struct kmt_epoll_data *wake = &wake_buffer[i];

            if (wake->fd >= 0)
            {
                /* Have to explicitly remove the epoll.
                 * If we have dup-ed fds, the epoll registration remains active.
                 * Need to do this before we signal the fence since signaling closes the fd,
                 * and a new FD might have been created before we get around to unreffing it from epoll.
                 */
                epoll_ctl(device->epoll_fd, EPOLL_CTL_DEL, wake->fd, NULL);
            }

            kmt_device_complete_fence(device, wake->order);
        }
    }

    return NULL;
}

kmt_device kmt_device_create(void)
{
    kmt_device device = vkd3d_calloc(1, sizeof(*device));
    struct epoll_event ev = { EPOLLIN };
    int fds[2];

    if (!device)
        return NULL;

    /* The epoll loop is supposed to run on the server process.
     * Client processes should have a unix socket or something like that
     * that can be used to send the occasional sync_file. */
    if (pipe2(fds, O_CLOEXEC) < 0)
        goto err;

    /* Writer must be blocking so we avoid losing wakeups spuriously under pressure. */
    if (fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK) < 0)
        goto close_pipe;

    device->poll_fd = fds[0];
    device->wake_fd = fds[1];
    device->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    epoll_ctl(device->epoll_fd, EPOLL_CTL_ADD, device->poll_fd, &ev);

    /* Replace with shmem flinging. */
    device->shmem = vkd3d_calloc(1, sizeof(*device->shmem));
    pthread_mutex_init(&device->shmem->lock, NULL);
    pthread_cond_init(&device->shmem->cond, NULL);

    pthread_mutex_init(&device->fence_lock, NULL);

    if (pthread_create(&device->epoll_thread, NULL, kmt_device_epoll_main, device))
        goto fail_thread;

    return device;

fail_thread:
    close(device->epoll_fd);
close_pipe:
    close(fds[0]);
    close(fds[1]);
err:
    vkd3d_free(device);
    return NULL;
}

void kmt_device_destroy(kmt_device device)
{
    struct kmt_epoll_data dummy = {0};
    size_t i;

    write(device->wake_fd, &dummy, sizeof(dummy));
    pthread_join(device->epoll_thread, NULL);

    pthread_mutex_destroy(&device->fence_lock);
    pthread_mutex_destroy(&device->shmem->lock);
    pthread_cond_destroy(&device->shmem->cond);
    for (i = 0; i < device->fences_count; i++)
        vkd3d_free(device->fences[i]);
    vkd3d_free(device->fences);

    close(device->epoll_fd);
    close(device->poll_fd);
    close(device->wake_fd);
    vkd3d_free(device);
}

static void kmt_device_register_fence(kmt_device device, struct kmt_fence_shmem_data *shmem)
{
    shmem->id = vkd3d_atomic_uint64_increment(&device->shmem->order, vkd3d_memory_order_relaxed);
    pthread_mutex_lock(&device->fence_lock);
    vkd3d_array_reserve((void **)&device->fences, &device->fences_size,
                        device->fences_count + 1, sizeof(*device->fences));
    device->fences[device->fences_count++] = shmem;
    pthread_mutex_unlock(&device->fence_lock);
}

kmt_handle kmt_device_create_fence(kmt_device device, uint64_t initial_value)
{
    struct kmt_fence_shmem_data *shmem;
    kmt_handle fence;
    fence = vkd3d_calloc(1, sizeof(*fence));
    if (!fence)
        return NULL;

    /* Cross-process style, replace this alloc with a server request for FD to shmem. */
    shmem = vkd3d_calloc(1, sizeof(*shmem));
    shmem->current_value = initial_value;
    pthread_mutex_init(&shmem->lock, NULL);
    pthread_cond_init(&shmem->cond, NULL);
    kmt_device_register_fence(device, shmem);

    fence->shmem = shmem;
    return fence;
}

void kmt_device_destroy_fence(kmt_device device, kmt_handle fence)
{
    struct kmt_fence_shmem_data *shmem = fence->shmem;
    uint64_t id = shmem->id;
    size_t i;

    /* Cross-process consideration. We cannot touch device directly here.
     * It might be possible to detect global fence destruction through other means,
     * e.g. a pipe FD could represent ownership and if the server process detects that the FD is closed
     * on the other end, the fence reference is dropped.
     * Once all references to a fence are dropped entirely, all waiters for that fence are automatically
     * unblocked somehow.
     */
    pthread_mutex_lock(&device->fence_lock);
    for (i = 0; i < device->fences_count; i++)
    {
        if (device->fences[i]->id == id)
        {
            device->fences[i] = device->fences[--device->fences_count];
            break;
        }
    }
    pthread_mutex_unlock(&device->fence_lock);

    pthread_mutex_destroy(&shmem->lock);
    pthread_cond_destroy(&shmem->cond);

    /* Replace with munmap. */
    vkd3d_free(shmem);
    vkd3d_free(fence);
}

uint64_t kmt_device_query_fence(kmt_device device, kmt_handle fence)
{
    struct kmt_fence_shmem_data *shmem = fence->shmem;
    uint64_t value;

    /* Need a lock unfortunately since fence values and events should be signaled atomically. */
    pthread_mutex_lock(&shmem->lock);
    value = shmem->current_value;
    pthread_mutex_unlock(&shmem->lock);

    return value;
}

static struct kmt_pending_edge *kmt_device_fence_find_edge_locked(struct kmt_fence_shmem_data *shmem, uint64_t edge)
{
    uint32_t i;
    for (i = 0; i < shmem->pending_edges_count; i++)
        if (shmem->pending_edges[i].edge == edge)
            return &shmem->pending_edges[i];
    return NULL;
}

bool kmt_device_fence_register_edge(
    kmt_device device, kmt_handle fence, uint64_t value, int eventfd, uint64_t *edge)
{
    struct kmt_fence_shmem_data *shmem = fence->shmem;
    struct kmt_pending_edge *pending_edge;

    pthread_mutex_lock(&shmem->lock);

    if (shmem->current_value >= value)
    {
        pthread_mutex_unlock(&shmem->lock);

        /* Immediately satisfied. */
        *edge = 0;

        if (eventfd >= 0)
        {
            const uint64_t dummy = 1;
            write(eventfd, &dummy, sizeof(dummy));
        }

        return true;
    }

    if (shmem->pending_edges_count == ARRAY_SIZE(shmem->pending_edges))
    {
        /* Docs for SetEvent says it's allowed to fail due to out of memory conditions. */
        pthread_mutex_unlock(&shmem->lock);
        return false;
    }

    pending_edge = &shmem->pending_edges[shmem->pending_edges_count++];
    pending_edge->edge = vkd3d_atomic_uint64_increment(&device->shmem->order, vkd3d_memory_order_relaxed);
    assert(eventfd != 0);
    pending_edge->eventfd = eventfd;
    pending_edge->syncfd = -1;
    pending_edge->value = value;

    *edge = pending_edge->edge;

    pthread_mutex_unlock(&shmem->lock);

    /* For cross-process, we should send the edge + eventfd FD so that server is able to signal the FD
     * if the edge is satisfied by a GPU signal later. */

    return true;
}

bool kmt_device_fence_edge_wait_materialization(kmt_device device, kmt_handle fence, uint64_t edge, int *sync_fd)
{
    struct kmt_fence_shmem_data *shmem = fence->shmem;
    struct kmt_pending_edge *pending_edge;
    bool first = true;
    int syncfd = -1;
    uint32_t i;

    if (edge == 0)
    {
        /* Trivial. */
        *sync_fd = -1;
        return true;
    }

    pthread_mutex_lock(&shmem->lock);
    for (;;)
    {
        pending_edge = kmt_device_fence_find_edge_locked(shmem, edge);
        if (!pending_edge)
        {
            /* We have already completed the wait. No need for FD. */
            *sync_fd = -1;
            break;
        }

        /* We have a binary semaphore. Used that to wait. */
        if (pending_edge->syncfd >= 0)
        {
            /* It's possible we have to wait for a FD to be sent from server here.
             * However, even if there is some latency in that operation, the GPU should be busy rendering
             * anyway. */
            *sync_fd = pending_edge->syncfd;

            /* Consume the edge now. */
            *pending_edge = shmem->pending_edges[--shmem->pending_edges_count];
            break;
        }
        else if (syncfd >= 0)
        {
            /* We got an immediate materialization. */
            *sync_fd = syncfd;
            syncfd = -1;

            /* Consume the edge now. */
            *pending_edge = shmem->pending_edges[--shmem->pending_edges_count];
            break;
        }
        else if (first)
        {
            struct kmt_device_shmem_data *device_shmem = device->shmem;
            uint64_t target_value = pending_edge->value;
            uint64_t lowest_order = UINT64_MAX;

            /* Try to fish for materialization immediately. If the materialization signal
             * has already been made by the time we registered the edge,
             * it will not update our pending edge and wake us up. Do not bother checking this every iteration.
             */

            /* Don't hold two locks at the same time. Could lead to deadlocks if we're not extremely
             * careful. */
            pthread_mutex_unlock(&shmem->lock);

            pthread_mutex_lock(&device_shmem->lock);
            {
                for (i = 0; i < device_shmem->pending_signal_count; i++)
                {
                    const struct kmt_pending_signal *pending_signal = &device_shmem->pending_signal[i];

                    /* There are multiple candidates, always prefers the lowest submit order.
                     * This is not a guarantee to be the first signal to actually make it,
                     * but it's best effort we can do. */
                    if (pending_signal->fence_id == shmem->id && pending_signal->value >= target_value &&
                        pending_signal->order < lowest_order)
                    {
                        syncfd = pending_signal->syncfd;
                        lowest_order = pending_signal->order;
                    }
                }

                /* Need to dup. If the signal is completed immediately after releasing the device lock,
                 * we won't have a valid FD anymore. This dup mirrors the one we make at sync_file registration. */
                if (syncfd >= 0)
                    syncfd = dup(syncfd);
            }
            pthread_mutex_unlock(&device_shmem->lock);

            /* Need to requery the pending_edge. Just restart the loop.
             * If we found a materializing signal, we'll consume that FD immediately. */
            pthread_mutex_lock(&shmem->lock);
            first = false;
            continue;
        }

        /* Block until either the wait is immediately satisfied by ID3D12Fence::Signal(),
         * or someone calls kmt_device_register_sync_file(). */
        pthread_cond_wait(&shmem->cond, &shmem->lock);
    }
    pthread_mutex_unlock(&shmem->lock);

    if (syncfd >= 0)
        close(syncfd);

    return true;
}

void kmt_device_fence_cancel_edge(kmt_device device, kmt_handle fence, uint64_t edge)
{
    struct kmt_fence_shmem_data *shmem = fence->shmem;
    struct kmt_pending_edge *pending_edge;

    pthread_mutex_lock(&shmem->lock);
    {
        pending_edge = kmt_device_fence_find_edge_locked(shmem, edge);
        if (pending_edge)
        {
            if (pending_edge->syncfd >= 0)
                close(pending_edge->syncfd);
            *pending_edge = shmem->pending_edges[--shmem->pending_edges_count];
        }
    }
    pthread_mutex_unlock(&shmem->lock);
}
