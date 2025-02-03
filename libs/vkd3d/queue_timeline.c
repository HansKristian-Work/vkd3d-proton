/*
 * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation
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
#include "vkd3d_platform.h"
#include "vkd3d_threads.h"
#include <assert.h>
#include <stdio.h>

#define NUM_ENTRIES (256 * 1024)

HRESULT vkd3d_queue_timeline_trace_init(struct vkd3d_queue_timeline_trace *trace, struct d3d12_device *device)
{
    char env[VKD3D_PATH_MAX];
    unsigned int i;

    if (!vkd3d_get_env_var("VKD3D_QUEUE_PROFILE", env, sizeof(env)))
        return S_OK;

    trace->file = fopen(env, "w");
    if (trace->file)
    {
        INFO("Creating timeline trace in: \"%s\".\n", env);
        fputs("[\n", trace->file);
    }
    else
        return S_OK;

    pthread_mutex_init(&trace->lock, NULL);
    pthread_mutex_init(&trace->ready_lock, NULL);

    vkd3d_array_reserve((void**)&trace->vacant_indices, &trace->vacant_indices_size,
            NUM_ENTRIES, sizeof(*trace->vacant_indices));

    /* Reserve entry 0 as sentinel. */
    for (i = 1; i < NUM_ENTRIES; i++)
        trace->vacant_indices[trace->vacant_indices_count++] = i;

    trace->state = vkd3d_calloc(NUM_ENTRIES, sizeof(*trace->state));
    trace->base_ts = vkd3d_get_current_time_ns();

    if (vkd3d_get_env_var("VKD3D_QUEUE_PROFILE_ABSOLUTE", env, sizeof(env)) &&
            env[0] == '1')
    {
        /* Wine logs are QPC relative */
        trace->base_ts = 0;

        /* Force an event at ts = 0 so the trace gets absolute time. */
        fprintf(trace->file,
                "{ \"name\": \"dummy\", \"ph\": \"i\", \"tid\": \"0x%04x\", \"pid\": 0, \"ts\": 0.0 },\n",
                vkd3d_get_current_thread_id());
    }

    trace->active = true;
    return S_OK;
}

static void vkd3d_queue_timeline_trace_free_index(struct vkd3d_queue_timeline_trace *trace, unsigned int index)
{
    assert(trace->state[index].type != VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_NONE);
    trace->state[index].type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_NONE;

    pthread_mutex_lock(&trace->lock);
    assert(trace->vacant_indices_count < trace->vacant_indices_size);
    trace->vacant_indices[trace->vacant_indices_count++] = index;
    pthread_mutex_unlock(&trace->lock);
}

static void vkd3d_queue_timeline_trace_free_indices(struct vkd3d_queue_timeline_trace *trace,
        const unsigned int *indices, size_t count)
{
    size_t i;
    pthread_mutex_lock(&trace->lock);

    for (i = 0; i < count; i++)
    {
        assert(trace->state[indices[i]].type != VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_NONE);
        trace->state[indices[i]].type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_NONE;
    }

    assert(trace->vacant_indices_count + count <= trace->vacant_indices_size);
    memcpy(trace->vacant_indices + trace->vacant_indices_count, indices, count * sizeof(*indices));
    trace->vacant_indices_count += count;
    pthread_mutex_unlock(&trace->lock);
}

static unsigned int vkd3d_queue_timeline_trace_allocate_index(struct vkd3d_queue_timeline_trace *trace, uint64_t *submit_count)
{
    unsigned int index = 0;
    pthread_mutex_lock(&trace->lock);
    if (trace->vacant_indices_count == 0)
    {
        ERR("Failed to allocate queue timeline index.\n");
        goto unlock;
    }
    index = trace->vacant_indices[--trace->vacant_indices_count];
unlock:
    if (submit_count)
        *submit_count = ++trace->submit_count;
    pthread_mutex_unlock(&trace->lock);
    return index;
}

void vkd3d_queue_timeline_trace_cleanup(struct vkd3d_queue_timeline_trace *trace)
{
    if (!trace->active)
        return;

    pthread_mutex_destroy(&trace->lock);
    pthread_mutex_destroy(&trace->ready_lock);
    if (trace->file)
        fclose(trace->file);

    vkd3d_free(trace->vacant_indices);
    vkd3d_free(trace->ready_command_lists);
    vkd3d_free(trace->state);
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_event_signal(struct vkd3d_queue_timeline_trace *trace,
        vkd3d_native_sync_handle handle, d3d12_fence_iface *fence, uint64_t value)
{
    struct vkd3d_queue_timeline_trace_cookie cookie = {0};
    struct vkd3d_queue_timeline_trace_state *state;

    if (!trace->active)
        return cookie;

    cookie.index = vkd3d_queue_timeline_trace_allocate_index(trace, NULL);
    if (!cookie.index)
        return cookie;

    state = &trace->state[cookie.index];
    state->type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_EVENT;
    state->start_ts = vkd3d_get_current_time_ns();

#ifdef _WIN32
    snprintf(state->desc, sizeof(state->desc), "event: %p, fence: %p, value %"PRIu64,
            handle.handle, (void*)fence, value);
#else
    snprintf(state->desc, sizeof(state->desc), "event: %d, fence: %p, value %"PRIu64,
            handle.fd, (void*)fence, value);
#endif

    return cookie;
}

void vkd3d_queue_timeline_trace_complete_event_signal(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_fence_worker *worker,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    const struct vkd3d_queue_timeline_trace_state *state;
    double end_ts, start_ts;
    unsigned int pid;

    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    end_ts = (double)(vkd3d_get_current_time_ns() - trace->base_ts) * 1e-3;
    start_ts = (double)(state->start_ts - trace->base_ts) * 1e-3;

    if (worker)
    {
        pid = worker->queue->submission_thread_tid;
        if (start_ts < worker->timeline.lock_end_event_ts)
            start_ts = worker->timeline.lock_end_event_ts;
        if (end_ts < start_ts)
            end_ts = start_ts;
        worker->timeline.lock_end_event_ts = end_ts;

        fprintf(trace->file, "{ \"name\": \"%s\", \"ph\": \"X\", \"tid\": \"event\", \"pid\": \"0x%04x\", \"ts\": %f, \"dur\": %f },\n",
                state->desc, pid, start_ts, end_ts - start_ts);
    }
    else
    {
        fprintf(trace->file, "{ \"name\": \"%s\", \"ph\": \"X\", \"tid\": \"inline\", \"pid\": \"shared fence\", \"ts\": %f, \"dur\": %f },\n",
                state->desc, start_ts, end_ts - start_ts);
    }

    vkd3d_queue_timeline_trace_free_index(trace, cookie.index);
}

void vkd3d_queue_timeline_trace_complete_present_wait(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    const struct vkd3d_queue_timeline_trace_state *state;
    double end_ts, start_ts;

    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    end_ts = (double)(vkd3d_get_current_time_ns() - trace->base_ts) * 1e-3;
    start_ts = (double)(state->start_ts - trace->base_ts) * 1e-3;

    fprintf(trace->file, "{ \"name\": \"%s\", \"ph\": \"X\", \"tid\": \"wait\", \"pid\": \"present\", \"ts\": %f, \"dur\": %f },\n",
            state->desc, start_ts, end_ts - start_ts);

    vkd3d_queue_timeline_trace_free_index(trace, cookie.index);
}

void vkd3d_queue_timeline_trace_complete_pso_compile(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie, uint64_t pso_hash, const char *completion_kind)
{
    const struct vkd3d_queue_timeline_trace_state *state;
    double end_ts, start_ts;
    unsigned int tid;

    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    end_ts = (double)(vkd3d_get_current_time_ns() - trace->base_ts) * 1e-3;
    start_ts = (double)(state->start_ts - trace->base_ts) * 1e-3;

    tid = vkd3d_get_current_thread_id();
    fprintf(trace->file, "{ \"name\": \"%016"PRIx64" %s\", \"ph\": \"X\", \"tid\": \"0x%04x\", \"pid\": \"pso\", \"ts\": %f, \"dur\": %f },\n",
            pso_hash, completion_kind, tid, start_ts, end_ts - start_ts);

    vkd3d_queue_timeline_trace_free_index(trace, cookie.index);
}

void vkd3d_queue_timeline_trace_complete_blocking(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie, const char *pid)
{
    const struct vkd3d_queue_timeline_trace_state *state;
    double end_ts, start_ts;

    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    end_ts = (double)(vkd3d_get_current_time_ns() - trace->base_ts) * 1e-3;
    start_ts = (double)(state->start_ts - trace->base_ts) * 1e-3;

    fprintf(trace->file, "{ \"name\": \"%s\", \"ph\": \"X\", \"tid\": \"0x%04x\", \"pid\": \"%s\", \"ts\": %f, \"dur\": %f },\n",
            state->desc, state->tid, pid, start_ts, end_ts - start_ts);

    vkd3d_queue_timeline_trace_free_index(trace, cookie.index);
}

void vkd3d_queue_timeline_trace_complete_present_block(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    vkd3d_queue_timeline_trace_complete_blocking(trace, cookie, "IDXGISwapChain::Present()");
}

void vkd3d_queue_timeline_trace_complete_low_latency_sleep(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    vkd3d_queue_timeline_trace_complete_blocking(trace, cookie, "ID3DLowLatencyDevice::LatencySleep()");
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_sparse(struct vkd3d_queue_timeline_trace *trace, uint32_t num_tiles)
{
    struct vkd3d_queue_timeline_trace_cookie cookie = {0};
    struct vkd3d_queue_timeline_trace_state *state;
    uint64_t submission_count;
    if (!trace->active)
        return cookie;

    cookie.index = vkd3d_queue_timeline_trace_allocate_index(trace, &submission_count);
    if (!cookie.index)
        return cookie;

    state = &trace->state[cookie.index];
    state->type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SUBMISSION;
    state->start_ts = vkd3d_get_current_time_ns();
    snprintf(state->desc, sizeof(state->desc), "SPARSE #%"PRIu64" (%u tiles)", submission_count, num_tiles);
    return cookie;
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_execute(struct vkd3d_queue_timeline_trace *trace,
        ID3D12CommandList * const *command_lists, unsigned int count)
{
    struct vkd3d_queue_timeline_trace_cookie cookie = {0};
    struct vkd3d_queue_timeline_trace_state *state;
    uint64_t submission_count;
    if (!trace->active)
        return cookie;

    cookie.index = vkd3d_queue_timeline_trace_allocate_index(trace, &submission_count);
    if (!cookie.index)
        return cookie;

    state = &trace->state[cookie.index];
    state->type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SUBMISSION;
    state->start_ts = vkd3d_get_current_time_ns();
    snprintf(state->desc, sizeof(state->desc), "SUBMIT #%"PRIu64" (%u lists)", submission_count, count);

    /* Might be useful later. */
    (void)command_lists;

    return cookie;
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_signal(struct vkd3d_queue_timeline_trace *trace,
        d3d12_fence_iface *fence, uint64_t value)
{
    struct vkd3d_queue_timeline_trace_cookie cookie = {0};
    struct vkd3d_queue_timeline_trace_state *state;
    if (!trace->active)
        return cookie;

    cookie.index = vkd3d_queue_timeline_trace_allocate_index(trace, NULL);
    if (!cookie.index)
        return cookie;

    state = &trace->state[cookie.index];
    state->type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SIGNAL;
    state->start_ts = vkd3d_get_current_time_ns();
    state->start_submit_ts = state->start_ts;
    snprintf(state->desc, sizeof(state->desc), "SIGNAL %p %"PRIu64, (void*)fence, value);
    return cookie;
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_wait(struct vkd3d_queue_timeline_trace *trace,
        d3d12_fence_iface *fence, uint64_t value)
{
    struct vkd3d_queue_timeline_trace_cookie cookie = {0};
    struct vkd3d_queue_timeline_trace_state *state;
    if (!trace->active)
        return cookie;

    cookie.index = vkd3d_queue_timeline_trace_allocate_index(trace, NULL);
    if (!cookie.index)
        return cookie;

    state = &trace->state[cookie.index];
    state->type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_WAIT;
    state->start_ts = vkd3d_get_current_time_ns();
    state->start_submit_ts = state->start_ts;
    snprintf(state->desc, sizeof(state->desc), "WAIT %p %"PRIu64, (void*)fence, value);
    return cookie;
}

static struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_generic_op(struct vkd3d_queue_timeline_trace *trace,
        enum vkd3d_queue_timeline_trace_state_type type, const char *tag)
{
    struct vkd3d_queue_timeline_trace_cookie cookie = {0};
    struct vkd3d_queue_timeline_trace_state *state;
    if (!trace->active)
        return cookie;

    cookie.index = vkd3d_queue_timeline_trace_allocate_index(trace, NULL);
    if (!cookie.index)
        return cookie;

    state = &trace->state[cookie.index];
    state->type = type;
    state->start_ts = vkd3d_get_current_time_ns();
    state->start_submit_ts = state->start_ts;
    state->tid = vkd3d_get_current_thread_id();
    vkd3d_strlcpy(state->desc, sizeof(state->desc), tag);
    return cookie;
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_swapchain_blit(struct vkd3d_queue_timeline_trace *trace, uint64_t present_id)
{
    char str[128];
    snprintf(str, sizeof(str), "PRESENT (id = %"PRIu64") (blit)", present_id);
    return vkd3d_queue_timeline_trace_register_generic_op(trace, VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PRESENT_BLIT, str);
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_command_list(struct vkd3d_queue_timeline_trace *trace)
{
    struct vkd3d_queue_timeline_trace_cookie cookie = {0};
    struct vkd3d_queue_timeline_trace_state *state;
    uint64_t submission_count;
    if (!trace->active)
        return cookie;

    cookie.index = vkd3d_queue_timeline_trace_allocate_index(trace, &submission_count);
    if (!cookie.index)
        return cookie;

    state = &trace->state[cookie.index];
    state->type = VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMAND_LIST;
    state->start_ts = vkd3d_get_current_time_ns();
    state->record_cookie = submission_count;
    return cookie;
}

void vkd3d_queue_timeline_trace_close_command_list(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    struct vkd3d_queue_timeline_trace_state *state;
    if (!trace->active || cookie.index == 0 || !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_QUEUE_PROFILE_EXTRA))
        return;

    state = &trace->state[cookie.index];
    state->record_end_ts = vkd3d_get_current_time_ns();
    state->tid = vkd3d_get_current_thread_id();

    /* Defer actual IO until fence workers are doing something. */
    pthread_mutex_lock(&trace->ready_lock);
    vkd3d_array_reserve((void**)&trace->ready_command_lists, &trace->ready_command_lists_size,
            trace->ready_command_lists_count + 1, sizeof(*trace->ready_command_lists));
    trace->ready_command_lists[trace->ready_command_lists_count++] = cookie.index;
    pthread_mutex_unlock(&trace->ready_lock);
}

void vkd3d_queue_timeline_trace_register_instantaneous(struct vkd3d_queue_timeline_trace *trace,
        enum vkd3d_queue_timeline_trace_state_type type, uint64_t value)
{
    struct vkd3d_queue_timeline_trace_state *state;
    unsigned int index;

    /* Most of the instantaneous events are very spammy and rarely show anything actionable. */
    if (type != VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_QUEUE_PRESENT &&
            !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_QUEUE_PROFILE_EXTRA))
        return;

    if (!trace->active)
        return;

    index = vkd3d_queue_timeline_trace_allocate_index(trace, NULL);
    if (!index)
        return;

    state = &trace->state[index];
    state->type = type;
    state->start_ts = vkd3d_get_current_time_ns();
    state->tid = vkd3d_get_current_thread_id();
    state->record_cookie = value;

    /* Defer actual IO until fence workers are doing something. */
    pthread_mutex_lock(&trace->ready_lock);
    vkd3d_array_reserve((void**)&trace->ready_command_lists, &trace->ready_command_lists_size,
            trace->ready_command_lists_count + 1, sizeof(*trace->ready_command_lists));
    trace->ready_command_lists[trace->ready_command_lists_count++] = index;
    pthread_mutex_unlock(&trace->ready_lock);
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_present_wait(struct vkd3d_queue_timeline_trace *trace, uint64_t present_id)
{
    char str[128];
    snprintf(str, sizeof(str), "WAIT (id = %"PRIu64")", present_id);
    return vkd3d_queue_timeline_trace_register_generic_op(trace, VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PRESENT_WAIT, str);
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_pso_compile(struct vkd3d_queue_timeline_trace *trace)
{
    /* Details are filled in later. */
    return vkd3d_queue_timeline_trace_register_generic_op(trace, VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PSO_COMPILATION, "");
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_generic_region(struct vkd3d_queue_timeline_trace *trace, const char *tag)
{
    return vkd3d_queue_timeline_trace_register_generic_op(trace, VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_GENERIC_REGION, tag);
}


struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_present_block(struct vkd3d_queue_timeline_trace *trace, uint64_t present_id)
{
    char str[128];
    snprintf(str, sizeof(str), "PRESENT (id = %"PRIu64")", present_id);
    return vkd3d_queue_timeline_trace_register_generic_op(trace, VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PRESENT_BLOCK, str);
}

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_low_latency_sleep(struct vkd3d_queue_timeline_trace *trace, uint64_t present_id)
{
    char str[128];
    snprintf(str, sizeof(str), "LATENCY SLEEP (id = %"PRIu64")", present_id);
    return vkd3d_queue_timeline_trace_register_generic_op(trace, VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_LOW_LATENCY_SLEEP, str);
}

static void vkd3d_queue_timeline_trace_flush_instantaneous(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_fence_worker *worker)
{
    const struct vkd3d_queue_timeline_trace_state *list_state;
    size_t list_count;
    size_t i;

    pthread_mutex_lock(&trace->ready_lock);
    if (trace->ready_command_lists_count)
    {
        /* Copy to local buffer to not stall recording threads while doing IO. */
        vkd3d_array_reserve((void**)&worker->timeline.list_buffer,
                &worker->timeline.list_buffer_size,
                trace->ready_command_lists_count, sizeof(*worker->timeline.list_buffer));
        memcpy(worker->timeline.list_buffer,
                trace->ready_command_lists, trace->ready_command_lists_count * sizeof(*worker->timeline.list_buffer));
        list_count = trace->ready_command_lists_count;
        trace->ready_command_lists_count = 0;
        pthread_mutex_unlock(&trace->ready_lock);

        for (i = 0; i < list_count; i++)
        {
            const char *generic_pid = NULL;
            double start_ts;
            double end_ts;

            list_state = &trace->state[worker->timeline.list_buffer[i]];
            start_ts = (double)(list_state->start_ts - trace->base_ts) * 1e-3;

            switch (list_state->type)
            {
                case VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMAND_LIST:
                {
                    end_ts = (double)(list_state->record_end_ts - trace->base_ts) * 1e-3;
                    fprintf(trace->file,
                            "{ \"name\": \"%"PRIu64 " (delay %.3f us)\", \"ph\": \"i\", \"tid\": \"0x%04x\", \"pid\": \"cmd reset\", \"ts\": %f },\n",
                            list_state->record_cookie, end_ts - start_ts, list_state->tid, start_ts);
                    fprintf(trace->file,
                            "{ \"name\": \"%"PRIu64" (delay %.3f us)\", \"ph\": \"i\", \"tid\": \"0x%04x\", \"pid\": \"cmd close\", \"ts\": %f },\n",
                            list_state->record_cookie, end_ts - start_ts, list_state->tid, end_ts);
                    break;
                }

                case VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_QUEUE_PRESENT:
                    generic_pid = "vkQueuePresentKHR";
                    break;

                case VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_HEAP_ALLOCATION:
                    generic_pid = "heap allocate";
                    break;

                case VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_VK_ALLOCATE_MEMORY:
                    generic_pid = "vkAllocateMemory";
                    break;

                case VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_CLEAR_ALLOCATION:
                    generic_pid = "clear allocation";
                    break;

                case VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMAND_ALLOCATOR_RESET:
                    generic_pid = "command allocator reset";
                    break;

                case VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMITTED_RESOURCE_ALLOCATION:
                    generic_pid = "committed resource alloc";
                    break;

                default:
                    break;
            }

            if (generic_pid)
            {
                fprintf(trace->file,
                        "{ \"name\": \"%"PRIu64"\", \"ph\": \"i\", \"tid\": \"0x%04x\", \"pid\": \"%s\", \"ts\": %f },\n",
                        list_state->record_cookie, list_state->tid, generic_pid, start_ts);
            }
        }

        vkd3d_queue_timeline_trace_free_indices(trace, worker->timeline.list_buffer, list_count);
    }
    else
        pthread_mutex_unlock(&trace->ready_lock);
}

void vkd3d_queue_timeline_trace_complete_execute(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_fence_worker *worker,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    double end_ts, start_submit_ts, start_ts, overhead_start_ts, overhead_end_ts;
    const struct vkd3d_queue_timeline_trace_state *state;
    unsigned int pid;
    const char *tid;
    double *ts_lock;

    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    start_ts = (double)(state->start_ts - trace->base_ts) * 1e-3;
    start_submit_ts = (double)(state->start_submit_ts - trace->base_ts) * 1e-3;
    end_ts = (double)(vkd3d_get_current_time_ns() - trace->base_ts) * 1e-3;
    overhead_start_ts = start_ts + 1e-3 * state->overhead_start_offset;
    overhead_end_ts = start_ts + 1e-3 * state->overhead_end_offset;

    if (worker)
    {
        if (state->type == VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SUBMISSION)
            vkd3d_queue_timeline_trace_flush_instantaneous(trace, worker);

        tid = worker->timeline.tid;
        if (state->type == VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_GENERIC_REGION)
            tid = "regions";

        pid = worker->queue->submission_thread_tid;

        if (state->type == VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SUBMISSION)
        {
            fprintf(trace->file, "{ \"name\": \"%s\", \"ph\": \"i\", \"tid\": \"cpu\", \"pid\": \"0x%04x\", \"ts\": %f, \"s\": \"t\" },\n",
                    state->desc, pid, start_ts);

            if (start_ts < worker->timeline.lock_end_cpu_ts)
                start_ts = worker->timeline.lock_end_cpu_ts;
            if (start_submit_ts < start_ts)
                start_submit_ts = start_ts;
        }

        if (state->type != VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_GENERIC_REGION)
        {
            ts_lock = &worker->timeline.lock_end_gpu_ts;

            if (start_submit_ts < *ts_lock)
                start_submit_ts = *ts_lock;
            if (end_ts < start_submit_ts)
                end_ts = start_submit_ts;
            *ts_lock = end_ts;
        }

        fprintf(trace->file, "{ \"name\": \"%s\", \"ph\": \"X\", \"tid\": \"%s\", \"pid\": \"0x%04x\", \"ts\": %f, \"dur\": %f },\n",
                state->desc, tid, pid, start_submit_ts, end_ts - start_submit_ts);

        if (state->type == VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SUBMISSION)
        {
            worker->timeline.lock_end_cpu_ts = start_submit_ts;
            fprintf(trace->file,
                    "{ \"name\": \"%s\", \"ph\": \"X\", \"tid\": \"submit\", \"pid\": \"0x%04x\", \"ts\": %f, \"dur\": %f },\n",
                    state->desc, pid, start_ts, start_submit_ts - start_ts);
            fprintf(trace->file,
                    "{ \"name\": \"%s\", \"ph\": \"X\", \"tid\": \"overhead\", \"pid\": \"0x%04x\", \"ts\": %f, \"dur\": %f },\n",
                    state->desc, pid, overhead_start_ts, overhead_end_ts - overhead_start_ts);
        }
    }

    vkd3d_queue_timeline_trace_free_index(trace, cookie.index);
}

void vkd3d_queue_timeline_trace_begin_execute(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    struct vkd3d_queue_timeline_trace_state *state;
    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    state->start_submit_ts = vkd3d_get_current_time_ns();
}

void vkd3d_queue_timeline_trace_begin_execute_overhead(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    struct vkd3d_queue_timeline_trace_state *state;
    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    state->overhead_start_offset = vkd3d_get_current_time_ns() - state->start_ts;
}

void vkd3d_queue_timeline_trace_end_execute_overhead(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie)
{
    struct vkd3d_queue_timeline_trace_state *state;
    if (!trace->active || cookie.index == 0)
        return;

    state = &trace->state[cookie.index];
    state->overhead_end_offset = vkd3d_get_current_time_ns() - state->start_ts;
}
