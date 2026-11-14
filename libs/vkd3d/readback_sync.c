/*
 * Copyright 2026 vkd3d-proton contributors
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

#include "vkd3d_private.h"

#ifndef _WIN32
#include <sched.h>
#endif

/* Workaround for applications that read a READBACK buffer on the CPU without
 * waiting for the copy that refills it (VKD3D_CONFIG=readback_map_sync).
 *
 * Death Stranding 2 copies a GPU-generated visibility list into a READBACK buffer
 * every frame and reads it in place from CPU jobs. Before reading, it waits for the
 * last fence value submitted on the queue, not for the submission that contains the
 * copy into this buffer. If the copy is recorded but not yet submitted, the wait
 * returns early and the CPU reads the buffer while the copy overwrites it, which
 * shows up as objects flickering in and out of the scene. Timing on Windows hides
 * this; with vkd3d-proton it happens in a few percent of frames.
 *
 * The app maps the buffer every frame before reading it, so we restore the missing
 * dependency in Map(): if a large copy into the buffer has been recorded, wait until
 * that command list has been submitted, the next Signal() on that queue has been
 * issued, and the fence has reached that value. The wait is bounded so that an app
 * that maps before it submits on the same thread cannot deadlock. */

#define VKD3D_READBACK_SYNC_MIN_BYTES (512 * 1024)
#define VKD3D_READBACK_SYNC_TIMEOUT_NS (4 * 1000 * 1000)

enum vkd3d_readback_sync_state
{
    VKD3D_READBACK_SYNC_IDLE = 0,
    VKD3D_READBACK_SYNC_RECORDED,
    VKD3D_READBACK_SYNC_SUBMITTED,
    VKD3D_READBACK_SYNC_SIGNALED,
};

static bool d3d12_resource_is_readback_buffer(const struct d3d12_resource *resource)
{
    const D3D12_HEAP_PROPERTIES *props = &resource->heap_properties;

    if (resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || (resource->flags & VKD3D_RESOURCE_RESERVED))
        return false;
    return props->Type == D3D12_HEAP_TYPE_READBACK ||
            (props->Type == D3D12_HEAP_TYPE_CUSTOM && props->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
}

static void vkd3d_readback_sync_entry_clear(struct vkd3d_readback_sync_entry *entry)
{
    if (entry->fence)
        ID3D12Fence_Release(entry->fence);
    entry->fence = NULL;
    entry->list = NULL;
    entry->queue = NULL;
    entry->state = VKD3D_READBACK_SYNC_IDLE;
}

void vkd3d_readback_sync_init(struct vkd3d_readback_sync *sync)
{
    memset(sync, 0, sizeof(*sync));
    pthread_mutex_init(&sync->mutex, NULL);
}

void vkd3d_readback_sync_cleanup(struct vkd3d_readback_sync *sync)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(sync->entries); i++)
        vkd3d_readback_sync_entry_clear(&sync->entries[i]);
    pthread_mutex_destroy(&sync->mutex);
}

void vkd3d_readback_sync_record_copy(struct vkd3d_readback_sync *sync, const void *list,
        struct d3d12_resource *dst, UINT64 byte_count)
{
    struct vkd3d_readback_sync_entry *entry = NULL;
    unsigned int i;

    if (byte_count < VKD3D_READBACK_SYNC_MIN_BYTES || !d3d12_resource_is_readback_buffer(dst))
        return;

    pthread_mutex_lock(&sync->mutex);
    for (i = 0; i < ARRAY_SIZE(sync->entries) && !entry; i++)
        if (sync->entries[i].resource == dst)
            entry = &sync->entries[i];
    for (i = 0; i < ARRAY_SIZE(sync->entries) && !entry; i++)
        if (sync->entries[i].state == VKD3D_READBACK_SYNC_IDLE)
            entry = &sync->entries[i];

    if (entry)
    {
        /* Entries are keyed by pointer only and are never dereferenced, so a
         * destroyed resource leaves a harmless idle entry behind. */
        vkd3d_readback_sync_entry_clear(entry);
        entry->resource = dst;
        entry->list = list;
        entry->state = VKD3D_READBACK_SYNC_RECORDED;
        entry->generation++;
    }
    pthread_mutex_unlock(&sync->mutex);
}

void vkd3d_readback_sync_reset_list(struct vkd3d_readback_sync *sync, const void *list)
{
    unsigned int i;

    pthread_mutex_lock(&sync->mutex);
    for (i = 0; i < ARRAY_SIZE(sync->entries); i++)
    {
        /* Recorded but reset without being submitted. */
        if (sync->entries[i].state == VKD3D_READBACK_SYNC_RECORDED && sync->entries[i].list == list)
            vkd3d_readback_sync_entry_clear(&sync->entries[i]);
    }
    pthread_mutex_unlock(&sync->mutex);
}

void vkd3d_readback_sync_submit(struct vkd3d_readback_sync *sync, const void *queue,
        ID3D12CommandList * const *lists, UINT count)
{
    unsigned int i, j;

    pthread_mutex_lock(&sync->mutex);
    for (i = 0; i < ARRAY_SIZE(sync->entries); i++)
    {
        if (sync->entries[i].state != VKD3D_READBACK_SYNC_RECORDED)
            continue;
        for (j = 0; j < count; j++)
        {
            if (sync->entries[i].list == (const void *)lists[j])
            {
                sync->entries[i].state = VKD3D_READBACK_SYNC_SUBMITTED;
                sync->entries[i].queue = queue;
                break;
            }
        }
    }
    pthread_mutex_unlock(&sync->mutex);
}

void vkd3d_readback_sync_signal(struct vkd3d_readback_sync *sync, const void *queue,
        ID3D12Fence *fence, UINT64 value)
{
    unsigned int i;

    if (!fence)
        return;

    pthread_mutex_lock(&sync->mutex);
    for (i = 0; i < ARRAY_SIZE(sync->entries); i++)
    {
        if (sync->entries[i].state == VKD3D_READBACK_SYNC_SUBMITTED && sync->entries[i].queue == queue)
        {
            ID3D12Fence_AddRef(fence);
            sync->entries[i].fence = fence;
            sync->entries[i].value = value;
            sync->entries[i].state = VKD3D_READBACK_SYNC_SIGNALED;
        }
    }
    pthread_mutex_unlock(&sync->mutex);
}

static void vkd3d_readback_sync_relax(unsigned int iteration)
{
    if (iteration < 128)
        vkd3d_pause();
    else
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
}

void vkd3d_readback_sync_wait_for_map(struct vkd3d_readback_sync *sync, struct d3d12_resource *resource)
{
    struct vkd3d_readback_sync_entry *entry = NULL;
    uint64_t start_ns, generation, value = 0;
    unsigned int i, iteration = 0;
    ID3D12Fence *fence = NULL;

    if (!d3d12_resource_is_readback_buffer(resource))
        return;

    pthread_mutex_lock(&sync->mutex);
    for (i = 0; i < ARRAY_SIZE(sync->entries) && !entry; i++)
        if (sync->entries[i].resource == resource && sync->entries[i].state != VKD3D_READBACK_SYNC_IDLE)
            entry = &sync->entries[i];
    if (!entry)
    {
        pthread_mutex_unlock(&sync->mutex);
        return;
    }

    generation = entry->generation;
    start_ns = vkd3d_get_current_time_ns();

    /* Wait for the copy to be submitted and for a Signal() to follow it. */
    while (entry->generation == generation &&
            (entry->state == VKD3D_READBACK_SYNC_RECORDED || entry->state == VKD3D_READBACK_SYNC_SUBMITTED))
    {
        pthread_mutex_unlock(&sync->mutex);
        if (vkd3d_get_current_time_ns() - start_ns > VKD3D_READBACK_SYNC_TIMEOUT_NS)
            goto timeout;
        vkd3d_readback_sync_relax(iteration++);
        pthread_mutex_lock(&sync->mutex);
    }

    if (entry->generation == generation && entry->state == VKD3D_READBACK_SYNC_SIGNALED)
    {
        fence = entry->fence;
        ID3D12Fence_AddRef(fence);
        value = entry->value;
    }
    pthread_mutex_unlock(&sync->mutex);

    if (!fence)
        return;

    /* Wait for the GPU to complete the copy. */
    while (ID3D12Fence_GetCompletedValue(fence) < value)
    {
        if (vkd3d_get_current_time_ns() - start_ns > VKD3D_READBACK_SYNC_TIMEOUT_NS)
        {
            ID3D12Fence_Release(fence);
            goto timeout;
        }
        vkd3d_readback_sync_relax(iteration++);
    }
    ID3D12Fence_Release(fence);

    pthread_mutex_lock(&sync->mutex);
    if (entry->generation == generation && entry->state == VKD3D_READBACK_SYNC_SIGNALED)
        vkd3d_readback_sync_entry_clear(entry);
    pthread_mutex_unlock(&sync->mutex);
    return;

timeout:
    WARN("Timed out waiting for pending copy into readback buffer %p.\n", resource);
}
