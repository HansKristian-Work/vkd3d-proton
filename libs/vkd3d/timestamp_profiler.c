/*
 * Copyright 2025 Hans-Kristian Arntzen for Valve Corporation
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

#include "vkd3d_timestamp_profiler.h"
#include "vkd3d_threads.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define NUM_IN_FLIGHT_TIMESTAMPS (256 * 1024)

#define TS_TRACE TRACE

struct vkd3d_timestamp_profiler_submitted_work
{
    uint32_t timestamp_index;
    uint32_t pso_index;
    enum vkd3d_pipeline_type pipeline_type;
    uint32_t dispatch_count;
};

struct vkd3d_timestamp_profiler_pso_state
{
    vkd3d_shader_hash_t pso_hash;
    vkd3d_shader_hash_t root_signature_hash;
    vkd3d_shader_hash_t hashes[VKD3D_MAX_SHADER_STAGES];
    uint32_t hashes_count;
    enum vkd3d_pipeline_type pipeline_type;

    uint64_t ps_invocations;
    uint64_t non_ps_invocations;
    uint64_t total_ticks;
    uint64_t dispatch_count;
};

struct vkd3d_timestamp_profiler
{
    struct d3d12_device *device;

    VkQueryPool timestamp_pool;
    VkQueryPool cs_invocation_pool;
    VkQueryPool vs_invocation_pool;
    VkQueryPool ms_invocation_pool;

    /* Ready timestamps to be allocated. */
    uint32_t *vacant_index_pool;
    size_t vacant_index_count;

    uint32_t *refcount_list;

    struct vkd3d_timestamp_profiler_pso_state *pso_states;
    size_t pso_states_count;
    size_t pso_states_size;

    pthread_mutex_t alloc_lock;
    pthread_mutex_t pso_lock;

    /* Async thread that resolves timestamps. */
    struct vkd3d_timestamp_profiler_submitted_work *ready_ring;
    size_t ready_ring_size;
    pthread_t thread;
    pthread_cond_t cond;
    pthread_mutex_t lock;
    uint64_t read_progress;
    uint64_t write_progress;

    uint32_t frame_count;

    bool dead;
};

void vkd3d_timestamp_profiler_register_pipeline_state(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_pipeline_state *state)
{
    struct vkd3d_timestamp_profiler_pso_state *pso_state;
    unsigned int i;

    if (!profiler)
        return;

    pthread_mutex_lock(&profiler->pso_lock);
    state->timestamp_profiler.pso_entry_index = profiler->pso_states_count;

    TS_TRACE("Registering PSO index %u\n", state->timestamp_profiler.pso_entry_index);

    vkd3d_array_reserve((void **)&profiler->pso_states, &profiler->pso_states_size,
            profiler->pso_states_count + 1, sizeof(*profiler->pso_states));
    pso_state = &profiler->pso_states[profiler->pso_states_count++];

    memset(pso_state, 0, sizeof(*pso_state));
    pso_state->pipeline_type = state->pipeline_type;

    if (state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
    {
        pso_state->hashes[0] = state->compute.code.meta.hash;
        pso_state->hashes_count = 1;
    }
    else
    {
        for (i = 0; i < state->graphics.stage_count; i++)
            pso_state->hashes[i] = state->graphics.code[i].meta.hash;
        pso_state->hashes_count = state->graphics.stage_count;
    }

    pso_state->pso_hash = vkd3d_pipeline_cache_compatibility_condense(&state->pipeline_cache_compat);
    pso_state->root_signature_hash = state->pipeline_cache_compat.root_signature_compat_hash;

    pthread_mutex_unlock(&profiler->pso_lock);
}

static void vkd3d_timestamp_profiler_decref_timestamp_index(struct vkd3d_timestamp_profiler *profiler,
        uint32_t timestamp_index)
{
    uint32_t count;
    assert(timestamp_index < NUM_IN_FLIGHT_TIMESTAMPS);
    count = vkd3d_atomic_uint32_decrement(&profiler->refcount_list[timestamp_index], vkd3d_memory_order_acq_rel);
    TS_TRACE("Decrementing timestamp %u to %u\n", timestamp_index, count);
    if (!count)
    {
        pthread_mutex_lock(&profiler->alloc_lock);
        TS_TRACE("Vacant index count = %u\n", profiler->vacant_index_count);
        assert(profiler->vacant_index_count < NUM_IN_FLIGHT_TIMESTAMPS);
        profiler->vacant_index_pool[profiler->vacant_index_count++] = timestamp_index;
        pthread_mutex_unlock(&profiler->alloc_lock);
    }
}

static bool vkd3d_timestamp_profiler_resolve_timestamp(struct vkd3d_timestamp_profiler *profiler,
        const struct vkd3d_timestamp_profiler_submitted_work *work)
{
    const struct vkd3d_vk_device_procs *vk_procs = &profiler->device->vk_procs;
    struct vkd3d_timestamp_profiler_pso_state *state;
    VkResult vr = VK_NOT_READY;
    bool has_active_invocation;
    uint64_t invocations[5];
    uint64_t tses[2];
    unsigned int i;

    TS_TRACE("Resolving timestamp %u, pso %u\n", work->timestamp_index, work->pso_index);

    if (VK_CALL(vkGetQueryPoolResults(profiler->device->vk_device, profiler->timestamp_pool,
            work->timestamp_index * 2, 2, sizeof(tses), tses, sizeof(tses[0]), VK_QUERY_RESULT_64_BIT)) != VK_SUCCESS)
    {
        return false;
    }

    switch (work->pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_GRAPHICS:
            vr = VK_CALL(vkGetQueryPoolResults(profiler->device->vk_device, profiler->vs_invocation_pool,
                    work->timestamp_index, 1, sizeof(uint64_t) * 5, invocations, sizeof(uint64_t) * 5,
                    VK_QUERY_RESULT_64_BIT));
            if (vr == VK_SUCCESS)
            {
                VK_CALL(vkResetQueryPool(profiler->device->vk_device, profiler->vs_invocation_pool,
                        work->timestamp_index, 1));
            }
            break;

        case VKD3D_PIPELINE_TYPE_COMPUTE:
            vr = VK_CALL(vkGetQueryPoolResults(profiler->device->vk_device, profiler->cs_invocation_pool,
                    work->timestamp_index, 1, sizeof(uint64_t) * 1, invocations, sizeof(uint64_t) * 1,
                    VK_QUERY_RESULT_64_BIT));
            if (vr == VK_SUCCESS)
            {
                VK_CALL(vkResetQueryPool(profiler->device->vk_device, profiler->cs_invocation_pool,
                        work->timestamp_index, 1));
            }
            break;

        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            if (profiler->ms_invocation_pool)
            {
                vr = VK_CALL(vkGetQueryPoolResults(profiler->device->vk_device, profiler->ms_invocation_pool,
                        work->timestamp_index, 1, sizeof(uint64_t) * 3, invocations, sizeof(uint64_t) * 3,
                        VK_QUERY_RESULT_64_BIT));
                if (vr == VK_SUCCESS)
                {
                    VK_CALL(vkResetQueryPool(profiler->device->vk_device, profiler->ms_invocation_pool,
                            work->timestamp_index, 1));
                }
            }
            break;

        default:
            break;
    }

    if (vr != VK_SUCCESS)
        return false;

    TS_TRACE("Resetting query pool %u\n", work->timestamp_index);
    VK_CALL(vkResetQueryPool(profiler->device->vk_device, profiler->timestamp_pool,
            work->timestamp_index * 2, 2));

    pthread_mutex_lock(&profiler->pso_lock);

    assert(work->pso_index < profiler->pso_states_count);
    state = &profiler->pso_states[work->pso_index];
    has_active_invocation = false;

    switch (work->pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_GRAPHICS:
            for (i = 0; i < 5; i++)
                if (invocations[i] != 0)
                    has_active_invocation = true;

            state->ps_invocations += invocations[2];
            /* Technically, counters for non-used stages are undefined, but we rely on implementations
             * not being nonsensical. */
            state->non_ps_invocations += invocations[0] + invocations[1] + invocations[3] + invocations[4];
            break;

        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            if (profiler->ms_invocation_pool)
            {
                for (i = 0; i < 3; i++)
                    if (invocations[i] != 0)
                        has_active_invocation = true;

                state->ps_invocations += invocations[0];
                state->non_ps_invocations += invocations[1] + invocations[2];
            }
            break;

        case VKD3D_PIPELINE_TYPE_COMPUTE:
            has_active_invocation = invocations[0] != 0;
            state->non_ps_invocations += invocations[0];
            break;

        default:
            break;
    }

    /* Don't increment if we don't have any active invocations.
     * This is relevant for e.g. indirect commands. We don't need to measure empty work. */
    if (has_active_invocation)
    {
        state->dispatch_count += work->dispatch_count;
        if (tses[1] >= tses[0])
            state->total_ticks += tses[1] - tses[0];
        else
            WARN("Ticks are non-monotonic %"PRIu64" > %"PRIu64".\n", tses[0], tses[1]);
    }

    pthread_mutex_unlock(&profiler->pso_lock);

    vkd3d_timestamp_profiler_decref_timestamp_index(profiler, work->timestamp_index);
    return true;
}

static void vkd3d_timestamp_profiler_flush(struct vkd3d_timestamp_profiler *profiler)
{
    struct vkd3d_timestamp_profiler_pso_state *state;
    char final_path_tmp[VKD3D_PATH_MAX];
    char final_path[VKD3D_PATH_MAX];
    char env[VKD3D_PATH_MAX];
    VKD3D_UNUSED size_t n;
    unsigned int j;
    FILE *file;
    size_t i;

    if (!vkd3d_get_env_var("VKD3D_TIMESTAMP_PROFILE", env, sizeof(env)))
        return;

    INFO("Flushing timestamp profile to \"%s\"\n", env);

#ifdef _WIN32
    /* Wine workaround. See pipeline cache implementation for rationale. */
    if (env[0] == '/')
        snprintf(final_path, sizeof(final_path), "Z:\\%s", env + 1);
    else
        strcpy(final_path, env);

    for (i = 0, n = strlen(final_path); i < n; i++)
        if (final_path[i] == '/')
            final_path[i] = '\\';
#else
    strcpy(final_path, env);
#endif

    strcpy(final_path_tmp, final_path);
    vkd3d_strlcat(final_path_tmp, sizeof(final_path_tmp), ".tmp");

    file = fopen(final_path_tmp, "w");
    if (!file)
    {
        ERR("Failed to open \"%s\".\n", final_path_tmp);
        return;
    }

    /* Flush out the data to file. */

    fprintf(file, "PSO Type,PSO Hash,Shader Hashes,Total Time (s),Non-PS invocations,PS invocations,Commands,RS Hash\n");
    fprintf(file, "INTERNAL,SWAPCHAIN,0,0,0,0,%u,0\n",
            vkd3d_atomic_uint32_exchange_explicit(&profiler->frame_count, 0, vkd3d_memory_order_relaxed));

    for (i = 0; i < profiler->pso_states_count; i++)
    {
        char hash_list[128];
        double total_time;
        const char *type;

        state = &profiler->pso_states[i];
        if (state->dispatch_count == 0)
            continue;

        hash_list[0] = '\0';

        switch (state->pipeline_type)
        {
            case VKD3D_PIPELINE_TYPE_GRAPHICS:
                type = "VS";
                TS_TRACE("GRAPHICS PSO:\n");
                break;
            case VKD3D_PIPELINE_TYPE_COMPUTE:
                type = "CS";
                TS_TRACE("COMPUTE PSO:\n");
                break;
            case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
                type = "MS";
                TS_TRACE("MESH GRAPHICS PSO:\n");
                break;
            default:
                type = "";
                break;
        }

        TS_TRACE("  PSO Hash: %016"PRIx64"\n", state->pso_hash);
        TS_TRACE("  RootSig Hash: %016"PRIx64"\n", state->root_signature_hash);

        for (j = 0; j < state->hashes_count; j++)
        {
            char tmp[16 + 1];
            TS_TRACE("  Shader Hash: %016"PRIx64"\n", state->hashes[j]);
            snprintf(tmp, sizeof(tmp), "%016"PRIx64, state->hashes[j]);
            if (j != 0)
                vkd3d_strlcat(hash_list, sizeof(hash_list), "+");
            vkd3d_strlcat(hash_list, sizeof(hash_list), tmp);
        }

        if (state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            TS_TRACE("  CS invocations: %"PRIu64"\n", state->non_ps_invocations);
            TS_TRACE("  Dispatches: %"PRIu64"\n", state->dispatch_count);
        }
        else
        {
            TS_TRACE("  Non-PS invocations: %"PRIu64"\n", state->non_ps_invocations);
            TS_TRACE("      PS invocations: %"PRIu64"\n", state->ps_invocations);
            TS_TRACE("  Draws: %"PRIu64"\n", state->dispatch_count);
        }

        total_time = 1e-9 * (double)state->total_ticks *
                profiler->device->device_info.properties2.properties.limits.timestampPeriod;
        TS_TRACE("  Total time: %.6f s\n", total_time);

        fprintf(file, "%s,%016"PRIx64",%s,%.9f,%"PRIu64",%"PRIu64",%"PRIu64",%016"PRIx64"\n",
                type, state->pso_hash, hash_list, total_time,
                state->non_ps_invocations, state->ps_invocations, state->dispatch_count,
                state->root_signature_hash);

        state->dispatch_count = 0;
        state->non_ps_invocations = 0;
        state->ps_invocations = 0;
        state->total_ticks = 0;
    }

    fclose(file);

    if (!vkd3d_file_rename_overwrite(final_path_tmp, final_path))
        ERR("Failed to rename %s to %s.\n", final_path_tmp, final_path);
}

static void *vkd3d_timestamp_profiler_thread(void *arg)
{
    struct vkd3d_timestamp_profiler *profiler = arg;
    uint64_t write_progress;
    uint64_t ts, next_ts;

    vkd3d_set_thread_name("vkd3d-ts-flush");
    ts = vkd3d_get_current_time_ns();

    for (;;)
    {
        pthread_mutex_lock(&profiler->lock);
        while (!profiler->dead && profiler->write_progress == profiler->read_progress)
            pthread_cond_wait(&profiler->cond, &profiler->lock);

        if (profiler->dead)
        {
            pthread_mutex_unlock(&profiler->lock);
            break;
        }

        write_progress = profiler->write_progress;
        pthread_mutex_unlock(&profiler->lock);

        while (profiler->read_progress < write_progress)
        {
            const struct vkd3d_timestamp_profiler_submitted_work *work;
            work = &profiler->ready_ring[profiler->read_progress & (profiler->ready_ring_size - 1)];

            /* Waiting for timestamps involves spinning on an atomic most of the time,
             * so just busysleep instead to not hammer this thread 100%. */
            while (!vkd3d_timestamp_profiler_resolve_timestamp(profiler, work))
            {
#ifdef _WIN32
                Sleep(10);
#else
                struct timespec dur;
                dur.tv_sec = 0;
                dur.tv_nsec = 10000000;
                nanosleep(&dur, NULL);
#endif
            }

            pthread_mutex_lock(&profiler->lock);
            profiler->read_progress++;
            pthread_cond_broadcast(&profiler->cond);
            pthread_mutex_unlock(&profiler->lock);
        }

        /* We cannot rely on clean destruction. Flush every 5 seconds. */
        next_ts = vkd3d_get_current_time_ns();
        if (ts + 5ull * 1000ull * 1000ull * 1000ull < next_ts)
        {
            pthread_mutex_lock(&profiler->pso_lock);
            vkd3d_timestamp_profiler_flush(profiler);
            pthread_mutex_unlock(&profiler->pso_lock);
            ts = next_ts;
        }
    }

    vkd3d_timestamp_profiler_flush(profiler);

    return NULL;
}

static void vkd3d_timestamp_profiler_free(struct vkd3d_timestamp_profiler *profiler)
{
    const struct vkd3d_vk_device_procs *vk_procs = &profiler->device->vk_procs;
    struct d3d12_device *device = profiler->device;

    pthread_mutex_destroy(&profiler->lock);
    pthread_mutex_destroy(&profiler->alloc_lock);
    pthread_mutex_destroy(&profiler->pso_lock);
    pthread_cond_destroy(&profiler->cond);
    vkd3d_free(profiler->ready_ring);
    vkd3d_free(profiler->vacant_index_pool);

    VK_CALL(vkDestroyQueryPool(device->vk_device, profiler->cs_invocation_pool, NULL));
    VK_CALL(vkDestroyQueryPool(device->vk_device, profiler->ms_invocation_pool, NULL));
    VK_CALL(vkDestroyQueryPool(device->vk_device, profiler->vs_invocation_pool, NULL));
    VK_CALL(vkDestroyQueryPool(device->vk_device, profiler->timestamp_pool, NULL));

    vkd3d_free(profiler);
}

static void vkd3d_timestamp_profiler_flush_active_state(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &profiler->device->vk_procs;
    struct vkd3d_timestamp_profiler_submitted_work work;

    /* Have to end the timestamp here. */
    if (!list->timestamp_profiler.active_timestamp_state)
        return;

    assert(list->timestamp_profiler.timestamp_index != UINT32_MAX);
    TS_TRACE("Write timestamp query pool %u\n", list->timestamp_profiler.timestamp_index);
    VK_CALL(vkCmdWriteTimestamp2(list->cmd.vk_command_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            profiler->timestamp_pool, 2 * list->timestamp_profiler.timestamp_index + 1));

    switch (list->timestamp_profiler.active_timestamp_state->pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_COMPUTE:
            VK_CALL(vkCmdEndQuery(list->cmd.vk_command_buffer,
                    profiler->cs_invocation_pool, list->timestamp_profiler.timestamp_index));
            break;

        case VKD3D_PIPELINE_TYPE_GRAPHICS:
            VK_CALL(vkCmdEndQuery(list->cmd.vk_command_buffer,
                    profiler->vs_invocation_pool, list->timestamp_profiler.timestamp_index));
            break;

        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            if (profiler->ms_invocation_pool)
            {
                VK_CALL(vkCmdEndQuery(list->cmd.vk_command_buffer,
                        profiler->ms_invocation_pool, list->timestamp_profiler.timestamp_index));
            }
            break;

        default:
            break;
    }

    TS_TRACE("Command list %p, adding timestamp %u\n", list, list->timestamp_profiler.timestamp_index);

    vkd3d_array_reserve((void **)&list->timestamp_profiler.work, &list->timestamp_profiler.work_size,
            list->timestamp_profiler.work_count + 1, sizeof(*list->timestamp_profiler.work));

    assert(list->timestamp_profiler.dispatch_count);

    work.pso_index = list->timestamp_profiler.active_timestamp_state->timestamp_profiler.pso_entry_index;
    work.timestamp_index = list->timestamp_profiler.timestamp_index;
    work.pipeline_type = list->timestamp_profiler.active_timestamp_state->pipeline_type;
    work.dispatch_count = list->timestamp_profiler.dispatch_count;
    list->timestamp_profiler.work[list->timestamp_profiler.work_count++] = work;

    list->timestamp_profiler.active_timestamp_state = NULL;
    list->timestamp_profiler.dispatch_count = 0;
    list->timestamp_profiler.timestamp_index = UINT32_MAX;
}

void vkd3d_timestamp_profiler_set_pipeline_state(
        struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list,
        struct d3d12_pipeline_state *state)
{
    if (!profiler)
        return;
    if (list->timestamp_profiler.active_timestamp_state == state)
        return;
    vkd3d_timestamp_profiler_flush_active_state(profiler, list);
}

void vkd3d_timestamp_profiler_end_command_buffer(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list)
{
    if (!profiler)
        return;
    vkd3d_timestamp_profiler_flush_active_state(profiler, list);
}

static void vkd3d_timestamp_profiler_incref_timestamp_index(struct vkd3d_timestamp_profiler *profiler,
        uint32_t timestamp_index)
{
    VKD3D_UNUSED uint32_t count;
    assert(timestamp_index < NUM_IN_FLIGHT_TIMESTAMPS);
    count = vkd3d_atomic_uint32_increment(&profiler->refcount_list[timestamp_index], vkd3d_memory_order_acq_rel);
    TS_TRACE("Incrementing timestamp %u to %u\n", timestamp_index, count);
}

static uint32_t vkd3d_timestamp_profiler_allocate_timestamp_index(struct vkd3d_timestamp_profiler *profiler)
{
    uint32_t index = UINT32_MAX;
    pthread_mutex_lock(&profiler->alloc_lock);

    if (profiler->vacant_index_count == 0)
    {
        ERR("Failed to allocate timestamp index.\n");
        goto unlock;
    }

    index = profiler->vacant_index_pool[--profiler->vacant_index_count];
    TS_TRACE("Allocate index %u\n", index);
    vkd3d_timestamp_profiler_incref_timestamp_index(profiler, index);

unlock:
    pthread_mutex_unlock(&profiler->alloc_lock);
    return index;
}

void vkd3d_timestamp_profiler_mark_pre_command(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs;

    if (!profiler)
        return;

    list->timestamp_profiler.dispatch_count++;

    if (!list->state || list->timestamp_profiler.active_timestamp_state)
        return;

#if 1
    /* For now, only profile compute. */
    if (list->state->pipeline_type != VKD3D_PIPELINE_TYPE_COMPUTE)
        return;
#endif

    vk_procs = &profiler->device->vk_procs;
    list->timestamp_profiler.timestamp_index = vkd3d_timestamp_profiler_allocate_timestamp_index(profiler);
    list->timestamp_profiler.active_timestamp_state = list->state;

    if (list->timestamp_profiler.timestamp_index == UINT32_MAX)
        return;

    TS_TRACE("Write timestamp %u\n", list->timestamp_profiler.timestamp_index);

    VK_CALL(vkCmdWriteTimestamp2(list->cmd.vk_command_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            profiler->timestamp_pool, 2 * list->timestamp_profiler.timestamp_index + 0));

    /* To make timestamps more reliable we don't want cross-talk between dispatches/draws as much as possible.
     * For now, only attempt to do this for compute. */
    if (!(list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE))
    {
        VkMemoryBarrier2 vk_barrier;
        VkDependencyInfo dep_info;

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        memset(&dep_info, 0, sizeof(dep_info));

        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;

        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }

    switch (list->timestamp_profiler.active_timestamp_state->pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_COMPUTE:
            VK_CALL(vkCmdBeginQuery(list->cmd.vk_command_buffer,
                    profiler->cs_invocation_pool, list->timestamp_profiler.timestamp_index, 0));
            break;

        case VKD3D_PIPELINE_TYPE_GRAPHICS:
            VK_CALL(vkCmdBeginQuery(list->cmd.vk_command_buffer,
                    profiler->vs_invocation_pool, list->timestamp_profiler.timestamp_index, 0));
            break;

        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            if (profiler->ms_invocation_pool)
            {
                VK_CALL(vkCmdBeginQuery(list->cmd.vk_command_buffer,
                        profiler->ms_invocation_pool, list->timestamp_profiler.timestamp_index, 0));
            }
            break;

        default:
            break;
    }
}

void vkd3d_timestamp_profiler_end_render_pass(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list)
{
    vkd3d_timestamp_profiler_flush_active_state(profiler, list);
}

void vkd3d_timestamp_profiler_reset_command_list(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list)
{
    size_t i;

    if (!profiler)
        return;

    TS_TRACE("Resetting command list %p\n", list);
    for (i = 0; i < list->timestamp_profiler.work_count; i++)
        vkd3d_timestamp_profiler_decref_timestamp_index(profiler, list->timestamp_profiler.work[i].timestamp_index);
    list->timestamp_profiler.resubmit_timeline = 0;
    list->timestamp_profiler.timestamp_index = UINT32_MAX;
    list->timestamp_profiler.active_timestamp_state = NULL;
    list->timestamp_profiler.work_count = 0;
}

static void vkd3d_timestamp_profiler_wait_available_submit_locked(struct vkd3d_timestamp_profiler *profiler,
        uint64_t timeline,
        size_t num_timestamps)
{
    if (timeline == 0)
        return;

    TS_TRACE("Waiting for timeline %"PRIu64", num timestamps %zu\n", timeline, num_timestamps);
    while (profiler->read_progress < timeline &&
            (profiler->write_progress - profiler->read_progress + num_timestamps) <= profiler->ready_ring_size)
    {
        pthread_cond_wait(&profiler->cond, &profiler->lock);
    }
}

void vkd3d_timestamp_profiler_submit_command_list(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list)
{
    size_t i;

    if (!profiler)
        return;

    TS_TRACE("Submitting list %p, %zu timestamps\n", list, list->timestamp_profiler.work_count);
    for (i = 0; i < list->timestamp_profiler.work_count; i++)
        vkd3d_timestamp_profiler_incref_timestamp_index(profiler, list->timestamp_profiler.work[i].timestamp_index);

    pthread_mutex_lock(&profiler->lock);

    /* Before we can resubmit, ensure that the timestamps have been consumed and appropriately reset. */
    vkd3d_timestamp_profiler_wait_available_submit_locked(profiler,
            list->timestamp_profiler.resubmit_timeline,
            list->timestamp_profiler.work_count);

    for (i = 0; i < list->timestamp_profiler.work_count; i++)
    {
        profiler->ready_ring[profiler->write_progress++ & (profiler->ready_ring_size - 1)] =
                list->timestamp_profiler.work[i];
    }

    /* Just to make sure other submission threads don't consume the signal.
     * We have to wake up the TS thread here. */
    pthread_cond_broadcast(&profiler->cond);

    if (list->timestamp_profiler.work_count)
        list->timestamp_profiler.resubmit_timeline = profiler->write_progress - 1;
    pthread_mutex_unlock(&profiler->lock);
}

static VkQueryPool vkd3d_timestamp_profiler_create_query_pool(struct d3d12_device *device,
        VkQueryType type, uint32_t count, VkQueryPipelineStatisticFlags pipeline_statistics)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkQueryPoolCreateInfo pool_info;
    VkQueryPool vk_query_pool;

    memset(&pool_info, 0, sizeof(pool_info));
    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.queryCount = count;
    pool_info.queryType = type;
    pool_info.pipelineStatistics = pipeline_statistics;

    if (VK_CALL(vkCreateQueryPool(device->vk_device, &pool_info, NULL, &vk_query_pool)) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VK_CALL(vkResetQueryPool(device->vk_device, vk_query_pool, 0, count));
    return vk_query_pool;
}

struct vkd3d_timestamp_profiler *vkd3d_timestamp_profiler_init(struct d3d12_device *device)
{
    struct vkd3d_timestamp_profiler *profiler;
    char env[VKD3D_PATH_MAX];
    unsigned int i;

    if (!vkd3d_get_env_var("VKD3D_TIMESTAMP_PROFILE", env, sizeof(env)))
        return NULL;

    INFO("Writing timestamp profile to \"%s\".\n", env);

    profiler = vkd3d_calloc(1, sizeof(*profiler));
    if (!profiler)
        return NULL;

    pthread_mutex_init(&profiler->lock, NULL);
    pthread_mutex_init(&profiler->alloc_lock, NULL);
    pthread_mutex_init(&profiler->pso_lock, NULL);
    pthread_cond_init(&profiler->cond, NULL);

    profiler->vacant_index_pool = vkd3d_malloc(NUM_IN_FLIGHT_TIMESTAMPS * sizeof(*profiler->vacant_index_pool));
    for (i = 0; i < NUM_IN_FLIGHT_TIMESTAMPS; i++)
        profiler->vacant_index_pool[i] = NUM_IN_FLIGHT_TIMESTAMPS - 1 - i;
    profiler->vacant_index_count = NUM_IN_FLIGHT_TIMESTAMPS;
    profiler->refcount_list = vkd3d_calloc(NUM_IN_FLIGHT_TIMESTAMPS, sizeof(*profiler->refcount_list));

    profiler->ready_ring = vkd3d_malloc(NUM_IN_FLIGHT_TIMESTAMPS * sizeof(*profiler->ready_ring));
    profiler->ready_ring_size = NUM_IN_FLIGHT_TIMESTAMPS;

    profiler->timestamp_pool = vkd3d_timestamp_profiler_create_query_pool(
            device, VK_QUERY_TYPE_TIMESTAMP, 2 * NUM_IN_FLIGHT_TIMESTAMPS, 0);

    profiler->vs_invocation_pool = vkd3d_timestamp_profiler_create_query_pool(
            device, VK_QUERY_TYPE_PIPELINE_STATISTICS, NUM_IN_FLIGHT_TIMESTAMPS,
            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT);

    if (device->device_info.mesh_shader_features.meshShaderQueries)
    {
        profiler->ms_invocation_pool = vkd3d_timestamp_profiler_create_query_pool(
                device, VK_QUERY_TYPE_PIPELINE_STATISTICS, NUM_IN_FLIGHT_TIMESTAMPS,
                VK_QUERY_PIPELINE_STATISTIC_MESH_SHADER_INVOCATIONS_BIT_EXT |
                        VK_QUERY_PIPELINE_STATISTIC_TASK_SHADER_INVOCATIONS_BIT_EXT |
                        VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT);
    }

    profiler->cs_invocation_pool = vkd3d_timestamp_profiler_create_query_pool(
            device, VK_QUERY_TYPE_PIPELINE_STATISTICS, NUM_IN_FLIGHT_TIMESTAMPS,
            VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT);

    profiler->device = device;
    if (pthread_create(&profiler->thread, NULL, vkd3d_timestamp_profiler_thread, profiler))
        goto fail_thread;

    return profiler;

fail_thread:
    vkd3d_timestamp_profiler_free(profiler);
    return NULL;
}

void vkd3d_timestamp_profiler_mark_frame_boundary(struct vkd3d_timestamp_profiler *profiler)
{
    if (!profiler)
        return;

    vkd3d_atomic_uint32_increment(&profiler->frame_count, vkd3d_memory_order_relaxed);
}

void vkd3d_timestamp_profiler_deinit(struct vkd3d_timestamp_profiler *profiler)
{
    if (!profiler)
        return;

    pthread_mutex_lock(&profiler->lock);
    profiler->dead = true;
    pthread_cond_signal(&profiler->cond);
    pthread_mutex_unlock(&profiler->lock);
    pthread_join(profiler->thread, NULL);

    vkd3d_timestamp_profiler_free(profiler);
}
