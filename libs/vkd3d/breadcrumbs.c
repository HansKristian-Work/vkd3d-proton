/*
 * Copyright 2022 Hans-Kristian Arntzen for Valve Corporation
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
#include "vkd3d_debug.h"
#include "vkd3d_common.h"
#include <assert.h>
#include <stdio.h>
#include "vkd3d_descriptor_debug.h"

/* Just allocate everything up front. This only consumes host memory anyways. */
#define MAX_COMMAND_LISTS (32 * 1024)

/* Questionable on 32-bit, but we don't really care. */
#define NV_ENCODE_CHECKPOINT(context, counter) ((void*) ((uintptr_t)(context) + (uintptr_t)MAX_COMMAND_LISTS * (counter)))
#define NV_CHECKPOINT_CONTEXT(ptr) ((uint32_t)((uintptr_t)(ptr) % MAX_COMMAND_LISTS))
#define NV_CHECKPOINT_COUNTER(ptr) ((uint32_t)((uintptr_t)(ptr) / MAX_COMMAND_LISTS))

static const char *vkd3d_breadcrumb_command_type_to_str(enum vkd3d_breadcrumb_command_type type)
{
    switch (type)
    {
        case VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER:
            return "top_marker";
        case VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER:
            return "bottom_marker";
        case VKD3D_BREADCRUMB_COMMAND_SET_SHADER_HASH:
            return "set_shader_hash";
        case VKD3D_BREADCRUMB_COMMAND_DRAW:
            return "draw";
        case VKD3D_BREADCRUMB_COMMAND_DRAW_INDEXED:
            return "draw_indexed";
        case VKD3D_BREADCRUMB_COMMAND_DISPATCH:
            return "dispatch";
        case VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT:
            return "execute_indirect";
        case VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_TEMPLATE:
            return "execute_indirect_template";
        case VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_PATCH_COMPUTE:
            return "execute_indirect_patch_compute";
        case VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_PATCH_STATE_COMPUTE:
            return "execute_indirect_patch_state_compute";
        case VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_UNROLL_COMPUTE:
            return "execute_indirect_unroll_compute";
        case VKD3D_BREADCRUMB_COMMAND_COPY:
            return "copy";
        case VKD3D_BREADCRUMB_COMMAND_COPY_TILES:
            return "copy_tiles";
        case VKD3D_BREADCRUMB_COMMAND_RESOLVE:
            return "resolve";
        case VKD3D_BREADCRUMB_COMMAND_WBI:
            return "wbi";
        case VKD3D_BREADCRUMB_COMMAND_RESOLVE_QUERY:
            return "resolve_query";
        case VKD3D_BREADCRUMB_COMMAND_GATHER_VIRTUAL_QUERY:
            return "gather_virtual_query";
        case VKD3D_BREADCRUMB_COMMAND_BUILD_RTAS:
            return "build_rtas";
        case VKD3D_BREADCRUMB_COMMAND_BUILD_OMM:
            return "build_omm";
        case VKD3D_BREADCRUMB_COMMAND_COPY_RTAS:
            return "copy_rtas";
        case VKD3D_BREADCRUMB_COMMAND_COPY_OMM:
            return "copy_omm";
        case VKD3D_BREADCRUMB_COMMAND_EMIT_RTAS_POSTBUILD:
            return "emit_rtas_postbuild";
        case VKD3D_BREADCRUMB_COMMAND_TRACE_RAYS:
            return "trace_rays";
        case VKD3D_BREADCRUMB_COMMAND_BARRIER:
            return "barrier";
        case VKD3D_BREADCRUMB_COMMAND_AUX32:
            return "aux32";
        case VKD3D_BREADCRUMB_COMMAND_AUX64:
            return "aux64";
        case VKD3D_BREADCRUMB_COMMAND_VBO:
            return "vbo";
        case VKD3D_BREADCRUMB_COMMAND_IBO:
            return "ibo";
        case VKD3D_BREADCRUMB_COMMAND_ROOT_TABLE:
            return "root_table";
        case VKD3D_BREADCRUMB_COMMAND_ROOT_DESC:
            return "root_desc";
        case VKD3D_BREADCRUMB_COMMAND_ROOT_CONST:
            return "root_const";
        case VKD3D_BREADCRUMB_COMMAND_TAG:
            return "tag";
        case VKD3D_BREADCRUMB_COMMAND_DISCARD:
            return "discard";
        case VKD3D_BREADCRUMB_COMMAND_CLEAR_INLINE:
            return "clear_inline";
        case VKD3D_BREADCRUMB_COMMAND_CLEAR_PASS:
            return "clear_pass";
        case VKD3D_BREADCRUMB_COMMAND_CLEAR_UAV:
            return "clear_uav";
        case VKD3D_BREADCRUMB_COMMAND_CLEAR_UAV_COPY:
            return "clear_uav_copy";
        case VKD3D_BREADCRUMB_COMMAND_DSTORAGE:
            return "dstorage";
        case VKD3D_BREADCRUMB_COMMAND_WORKGRAPH_META:
            return "workgraph meta";
        case VKD3D_BREADCRUMB_COMMAND_WORKGRAPH_NODE:
            return "workgraph node";
        case VKD3D_BREADCRUMB_COMMAND_SYNC_VAL_CLEAR:
            return "sync-val clear";

        default:
            return "?";
    }
}

void vkd3d_breadcrumb_tracer_init_barrier_hashes(struct vkd3d_breadcrumb_tracer *tracer)
{
    pthread_mutex_init(&tracer->barrier_hash_lock, NULL);
    vkd3d_breadcrumb_tracer_update_barrier_hashes(tracer);
}

void vkd3d_breadcrumb_tracer_cleanup_barrier_hashes(struct vkd3d_breadcrumb_tracer *tracer)
{
    pthread_mutex_destroy(&tracer->barrier_hash_lock);
    vkd3d_free(tracer->barrier_hashes);
}

HRESULT vkd3d_breadcrumb_tracer_init(struct vkd3d_breadcrumb_tracer *tracer, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC1 resource_desc;
    VkMemoryPropertyFlags memory_props;
    HRESULT hr;
    int rc;

    memset(tracer, 0, sizeof(*tracer));

    if ((rc = pthread_mutex_init(&tracer->lock, NULL)))
        return hresult_from_errno(rc);

    if (device->vk_info.NV_device_diagnostic_checkpoints)
    {
        INFO("Enabling NV_device_diagnostics_checkpoints breadcrumbs.\n");
    }
    else if (device->vk_info.AMD_buffer_marker)
    {
        INFO("Enabling AMD_buffer_marker breadcrumbs.\n");
        memset(&resource_desc, 0, sizeof(resource_desc));
        resource_desc.Width = MAX_COMMAND_LISTS * sizeof(struct vkd3d_breadcrumb_counter);
        resource_desc.Height = 1;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (FAILED(hr = vkd3d_create_buffer(device, &heap_properties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
                &resource_desc, "breadcrumb-host-buffer", &tracer->host_buffer)))
        {
            goto err;
        }

        memory_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

        /* If device faults in the middle of execution we will never get the chance to flush device caches.
         * Make sure that breadcrumbs are always written directly out.
         * This is the primary usecase for the device coherent/uncached extension after all ...
         * Don't make this a hard requirement since buffer markers might be implicitly coherent on some
         * implementations (Turnip?). */
        if (device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory)
        {
            memory_props |= VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                    VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;
        }

        if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, tracer->host_buffer,
                memory_props, &tracer->host_buffer_memory)))
        {
            goto err;
        }

        if (VK_CALL(vkMapMemory(device->vk_device, tracer->host_buffer_memory.vk_memory,
                0, VK_WHOLE_SIZE,
                0, (void**)&tracer->mapped)) != VK_SUCCESS)
        {
            hr = E_OUTOFMEMORY;
            goto err;
        }

        memset(tracer->mapped, 0, sizeof(*tracer->mapped) * MAX_COMMAND_LISTS);
    }
    else
    {
        ERR("Breadcrumbs require support for either AMD_buffer_marker or NV_device_diagnostics_checkpoints.\n");
        hr = E_FAIL;
        goto err;
    }

    tracer->trace_contexts = vkd3d_calloc(MAX_COMMAND_LISTS, sizeof(*tracer->trace_contexts));
    tracer->trace_context_index = 0;

    return S_OK;

err:
    vkd3d_breadcrumb_tracer_cleanup(tracer, device);
    return hr;
}

void vkd3d_breadcrumb_tracer_cleanup(struct vkd3d_breadcrumb_tracer *tracer, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    if (device->vk_info.AMD_buffer_marker)
    {
        VK_CALL(vkDestroyBuffer(device->vk_device, tracer->host_buffer, NULL));
        vkd3d_free_device_memory(device, &tracer->host_buffer_memory);
    }

    vkd3d_free(tracer->trace_contexts);
    pthread_mutex_destroy(&tracer->lock);
}

unsigned int vkd3d_breadcrumb_tracer_allocate_command_list(struct vkd3d_breadcrumb_tracer *tracer,
        struct d3d12_command_list *list, struct d3d12_command_allocator *allocator)
{
    unsigned int index = UINT32_MAX;
    unsigned int iteration_count;
    int rc;

    if ((rc = pthread_mutex_lock(&tracer->lock)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return UINT32_MAX;
    }

    /* Since this is a ring, this is extremely likely to succeed on first attempt. */
    for (iteration_count = 0; iteration_count < MAX_COMMAND_LISTS; iteration_count++)
    {
        tracer->trace_context_index = (tracer->trace_context_index + 1) % MAX_COMMAND_LISTS;
        if (!tracer->trace_contexts[tracer->trace_context_index].locked)
        {
            tracer->trace_contexts[tracer->trace_context_index].locked = 1;
            index = tracer->trace_context_index;
            break;
        }
    }

    pthread_mutex_unlock(&tracer->lock);

    if (index == UINT32_MAX)
    {
        ERR("Failed to allocate new index for command list.\n");
        return index;
    }

    TRACE("Allocating breadcrumb context %u for list %p.\n", index, list);
    list->breadcrumb_context_index = index;

    /* Need to clear this on a fresh allocation rather than release, since we can end up releasing a command list
     * before we observe the device lost. */
    tracer->trace_contexts[index].command_count = 0;
    tracer->trace_contexts[index].counter = 0;

    if (!list->device->vk_info.NV_device_diagnostic_checkpoints && list->device->vk_info.AMD_buffer_marker)
        memset(&tracer->mapped[index], 0, sizeof(tracer->mapped[index]));

    vkd3d_array_reserve((void**)&allocator->breadcrumb_context_indices, &allocator->breadcrumb_context_index_size,
            allocator->breadcrumb_context_index_count + 1,
            sizeof(*allocator->breadcrumb_context_indices));
    allocator->breadcrumb_context_indices[allocator->breadcrumb_context_index_count++] = index;
    return index;
}

/* Command allocator keeps a list of allocated breadcrumb command lists. */
void vkd3d_breadcrumb_tracer_release_command_lists(struct vkd3d_breadcrumb_tracer *tracer,
        const unsigned int *indices, size_t indices_count)
{
    unsigned int index;
    size_t i;
    int rc;

    if (!indices_count)
        return;

    if ((rc = pthread_mutex_lock(&tracer->lock)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return;
    }

    for (i = 0; i < indices_count; i++)
    {
        index = indices[i];
        if (index != UINT32_MAX)
            tracer->trace_contexts[index].locked = 0;
        TRACE("Releasing breadcrumb context %u.\n", index);
    }
    pthread_mutex_unlock(&tracer->lock);
}

static pthread_mutex_t global_report_lock = PTHREAD_MUTEX_INITIALIZER;

static void vkd3d_breadcrumb_tracer_report_command_list(
        const struct vkd3d_breadcrumb_command_list_trace_context *context,
        uint32_t begin_marker,
        uint32_t end_marker)
{
    const struct vkd3d_breadcrumb_command *cmd;
    bool observed_begin_cmd = false;
    bool observed_end_cmd = false;
    bool ignore_markers;
    unsigned int i;

    ignore_markers = begin_marker == UINT32_MAX && end_marker == UINT32_MAX;

    if (end_marker == 0)
    {
        ERR(" ===== Potential crash region BEGIN (make sure RADV_DEBUG=syncshaders or VKD3D_CONFIG=breadcrumbs_sync is used for maximum accuracy) =====\n");
        observed_begin_cmd = true;
    }

    /* We can assume that possible culprit commands lie between the end_marker
     * and top_marker. */
    for (i = 0; i < context->command_count; i++)
    {
        cmd = &context->commands[i];

        if (ignore_markers && (cmd->type == VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER ||
                cmd->type == VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER))
        {
            continue;
        }

        /* If there is a command which sets TOP_OF_PIPE, but we haven't observed the marker yet,
         * the command processor hasn't gotten there yet (most likely ...), so that should be the
         * natural end-point. */
        if (!observed_end_cmd &&
                cmd->type == VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER &&
                cmd->count > begin_marker)
        {
            observed_end_cmd = true;
            ERR(" ===== Potential crash region END =====\n");
        }

        if (cmd->type == VKD3D_BREADCRUMB_COMMAND_AUX32)
        {
            ERR(" Set arg: %u (#%x)\n", cmd->word_32bit, cmd->word_32bit);
        }
        else if (cmd->type == VKD3D_BREADCRUMB_COMMAND_AUX64)
        {
            ERR(" Set arg: %"PRIu64" (#%"PRIx64")\n", cmd->word_64bit, cmd->word_64bit);
        }
        else if (cmd->type == VKD3D_BREADCRUMB_COMMAND_COOKIE)
        {
            ERR(" Cookie: %"PRIu64" (#%"PRIx64")\n", cmd->word_64bit, cmd->word_64bit);
        }
        else if (cmd->type == VKD3D_BREADCRUMB_COMMAND_TAG)
        {
            ERR("     Tag: %s\n", cmd->tag);
        }
        else
        {
            ERR("  Command: %s\n", vkd3d_breadcrumb_command_type_to_str(cmd->type));

            switch (cmd->type)
            {
                case VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER:
                case VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER:
                    ERR("    marker: %u\n", cmd->count);
                    break;

                case VKD3D_BREADCRUMB_COMMAND_SET_SHADER_HASH:
                    ERR("    hash: %016"PRIx64", stage: %x\n", cmd->shader.hash, cmd->shader.stage);
                    break;

                default:
                    break;
            }
        }

        /* We have proved we observed this command is complete.
         * Some command after this signal is at fault. */
        if (!observed_begin_cmd &&
                cmd->type == VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER &&
                cmd->count == end_marker)
        {
            observed_begin_cmd = true;
            ERR(" ===== Potential crash region BEGIN (make sure RADV_DEBUG=syncshaders is used for maximum accuracy) =====\n");
        }
    }
}

static void vkd3d_breadcrumb_tracer_report_command_list_linked(struct vkd3d_breadcrumb_tracer *tracer,
        uint32_t begin_context_index, uint32_t end_context_index)
{
    unsigned int count = 0;
    while (begin_context_index != end_context_index)
    {
        ERR("=== Replaying earlier command list #%u (context %u) in submission for clarity ===\n",
                count, begin_context_index);
        vkd3d_breadcrumb_tracer_report_command_list(&tracer->trace_contexts[begin_context_index],
                UINT32_MAX, UINT32_MAX);
        ERR("=====================================================\n");

        begin_context_index = tracer->trace_contexts[begin_context_index].next;
        count++;
    }
}

void vkd3d_breadcrumb_tracer_dump_command_list(struct vkd3d_breadcrumb_tracer *tracer,
        unsigned int index)
{
    /* Avoid interleaved logs when multiple threads submit. */
    pthread_mutex_lock(&global_report_lock);
    vkd3d_breadcrumb_tracer_report_command_list(&tracer->trace_contexts[index],
            UINT32_MAX, UINT32_MAX);
    pthread_mutex_unlock(&global_report_lock);
}

static bool vkd3d_breadcrumb_trace_contexts_are_linked(struct vkd3d_breadcrumb_tracer *tracer,
        uint32_t context_index, uint32_t target_context_index)
{
    while (context_index != target_context_index)
    {
        uint32_t next = tracer->trace_contexts[context_index].next;
        if (next != UINT32_MAX && next != context_index && tracer->trace_contexts[next].prev == context_index)
            context_index = next;
    }

    return context_index == target_context_index;
}

static uint32_t vkd3d_breadcrumb_tracer_rewind_linked_contexts(struct vkd3d_breadcrumb_tracer *tracer,
        uint32_t context_index)
{
    const struct vkd3d_breadcrumb_command_list_trace_context *context = &tracer->trace_contexts[context_index];
    while (context->prev != UINT32_MAX &&
            context->prev != context_index && /* avoid infinite loop if there is corruption. */
            tracer->trace_contexts[context->prev].next == context_index)
    {
        /* Make sure that prev and next link together as a sanity check, i.e., we have a reasonable linked list. */
        context_index = context->prev;
        context = &tracer->trace_contexts[context_index];
    }

    return context_index;
}

static void vkd3d_breadcrumb_tracer_report_command_list_amd(struct vkd3d_breadcrumb_tracer *tracer,
        unsigned int context_index)
{
    const struct vkd3d_breadcrumb_command_list_trace_context *context;
    uint32_t begin_context_index;
    uint32_t begin_marker;
    uint32_t end_marker;

    context = &tracer->trace_contexts[context_index];

    /* Unused, cannot be the cause. */
    if (context->counter == 0)
        return;

    begin_marker = tracer->mapped[context_index].begin_marker;
    end_marker = tracer->mapped[context_index].end_marker;

    /* Never executed, cannot be the cause. */
    if (begin_marker == 0 && end_marker == 0)
        return;

    /* Successfully retired, cannot be the cause. */
    if (begin_marker == UINT32_MAX && end_marker == UINT32_MAX)
        return;

    /* Edge case if we re-submitted a command list,
     * but it ends up crashing before we hit any BOTTOM_OF_PIPE
     * marker. Normalize the inputs such that end_marker <= begin_marker. */
    if (begin_marker > 0 && end_marker == UINT32_MAX)
        end_marker = 0;

    ERR("Found pending command list context %u in executable state, TOP_OF_PIPE marker %u, BOTTOM_OF_PIPE marker %u.\n",
            context_index, begin_marker, end_marker);

    /* If we had multiple lists in the batch, rewind the replayer. */
    begin_context_index = vkd3d_breadcrumb_tracer_rewind_linked_contexts(tracer, context_index);
    vkd3d_breadcrumb_tracer_report_command_list_linked(tracer, begin_context_index, context_index);
    vkd3d_breadcrumb_tracer_report_command_list(context, begin_marker, end_marker);
    ERR("Done analyzing command list.\n");
}

static void vkd3d_breadcrumb_tracer_report_queue_nv(struct vkd3d_breadcrumb_tracer *tracer,
        struct d3d12_device *device,
        VkQueue vk_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    uint32_t top_marker, bottom_marker;
    uint32_t checkpoint_context_index;
    VkCheckpointDataNV *checkpoints;
    uint32_t bottom_context_index;
    uint32_t begin_context_index;
    uint32_t checkpoint_marker;
    uint32_t top_context_index;
    uint32_t checkpoint_count;
    uint32_t i;

    VK_CALL(vkGetQueueCheckpointDataNV(vk_queue, &checkpoint_count, NULL));
    if (checkpoint_count == 0)
        return;

    checkpoints = vkd3d_calloc(checkpoint_count, sizeof(VkCheckpointDataNV));
    for (i = 0; i < checkpoint_count; i++)
        checkpoints[i].sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
    VK_CALL(vkGetQueueCheckpointDataNV(vk_queue, &checkpoint_count, checkpoints));

    bottom_context_index = UINT32_MAX;
    top_context_index = UINT32_MAX;
    bottom_marker = 0;
    top_marker = 0;

    for (i = 0; i < checkpoint_count; i++)
    {
        checkpoint_context_index = NV_CHECKPOINT_CONTEXT(checkpoints[i].pCheckpointMarker);
        checkpoint_marker = NV_CHECKPOINT_COUNTER(checkpoints[i].pCheckpointMarker);

        if (checkpoints[i].stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT && checkpoint_marker > top_marker)
        {
            /* We want to find the latest TOP_OF_PIPE_BIT. Then we prove that command processor got to that point. */
            top_marker = checkpoint_marker;
            top_context_index = checkpoint_context_index;
        }
        else if (checkpoints[i].stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT && checkpoint_marker > bottom_marker)
        {
            /* We want to find the latest BOTTOM_OF_PIPE_BIT. Then we prove that we got that far. */
            bottom_marker = checkpoint_marker;
            bottom_context_index = checkpoint_context_index;
        }
        else if (checkpoints[i].stage != VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT &&
                checkpoints[i].stage != VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
        {
            FIXME("Unexpected checkpoint pipeline stage. #%x\n", checkpoints[i].stage);
            continue;
        }
    }

    if (top_context_index != UINT32_MAX && bottom_context_index != UINT32_MAX &&
            top_marker != 0 && bottom_marker != 0 && bottom_marker != UINT32_MAX)
    {
        ERR("Found pending command list context [%u, %u] in executable state, TOP_OF_PIPE marker %u, BOTTOM_OF_PIPE marker %u.\n",
                bottom_context_index, top_context_index, top_marker, bottom_marker);

        begin_context_index = vkd3d_breadcrumb_tracer_rewind_linked_contexts(tracer, bottom_context_index);
        vkd3d_breadcrumb_tracer_report_command_list_linked(tracer, begin_context_index, bottom_context_index);

        if (bottom_context_index == top_context_index)
        {
            vkd3d_breadcrumb_tracer_report_command_list(&tracer->trace_contexts[bottom_context_index],
                    top_marker, bottom_marker);
        }
        else if (vkd3d_breadcrumb_trace_contexts_are_linked(tracer, bottom_context_index, top_context_index))
        {
            /* While a bit confusing, bottom context completes later than top context, so it comes first. */
            vkd3d_breadcrumb_tracer_report_command_list(&tracer->trace_contexts[bottom_context_index],
                    UINT32_MAX, bottom_marker);
            ERR(" ===== End of command buffer, but potential crash region goes beyond this point =====\n");
            vkd3d_breadcrumb_tracer_report_command_list_linked(tracer,
                    tracer->trace_contexts[bottom_context_index].next, top_context_index);
            vkd3d_breadcrumb_tracer_report_command_list(&tracer->trace_contexts[top_context_index],
                    top_marker, 0);
        }
        else
        {
            ERR("BOTTOM and TOP contexts are not linked. Something must be corrupt.\n");
        }

        ERR("Done analyzing command list.\n");
    }

    vkd3d_free(checkpoints);
}

void vkd3d_breadcrumb_tracer_report_device_lost(struct vkd3d_breadcrumb_tracer *tracer,
        struct d3d12_device *device)
{
    struct vkd3d_queue_family_info *queue_family_info;
    VkQueue vk_queue;
    unsigned int i;

    /* There may be latent information in the QA checker. */
    vkd3d_descriptor_debug_kick_qa_check(device->descriptor_qa_global_info);

    /* Avoid interleaved logs when multiple threads observe device lost. */
    pthread_mutex_lock(&global_report_lock);
    ERR("Device lost observed, analyzing breadcrumbs ...\n");

    if (tracer->reported_fault)
    {
        pthread_mutex_unlock(&global_report_lock);
        return;
    }
    tracer->reported_fault = true;

    if (device->vk_info.NV_device_diagnostic_checkpoints)
    {
        /* vkGetQueueCheckpointDataNV does not require us to synchronize access to the queue. */
        queue_family_info = d3d12_device_get_vkd3d_queue_family(device, D3D12_COMMAND_LIST_TYPE_DIRECT, VK_QUEUE_FAMILY_IGNORED);

        for (i = 0; i < queue_family_info->queue_count; i++)
        {
            vk_queue = queue_family_info->queues[i]->vk_queue;
            vkd3d_breadcrumb_tracer_report_queue_nv(tracer, device, vk_queue);
        }

        queue_family_info = d3d12_device_get_vkd3d_queue_family(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, VK_QUEUE_FAMILY_IGNORED);

        for (i = 0; i < queue_family_info->queue_count; i++)
        {
            vk_queue = queue_family_info->queues[i]->vk_queue;
            vkd3d_breadcrumb_tracer_report_queue_nv(tracer, device, vk_queue);
        }

        queue_family_info = d3d12_device_get_vkd3d_queue_family(device, D3D12_COMMAND_LIST_TYPE_COPY, VK_QUEUE_FAMILY_IGNORED);

        for (i = 0; i < queue_family_info->queue_count; i++)
        {
            vk_queue = queue_family_info->queues[i]->vk_queue;
            vkd3d_breadcrumb_tracer_report_queue_nv(tracer, device, vk_queue);
        }
    }
    else if (device->vk_info.AMD_buffer_marker)
    {
        /* AMD path, buffer marker. */
        for (i = 0; i < MAX_COMMAND_LISTS; i++)
            vkd3d_breadcrumb_tracer_report_command_list_amd(tracer, i);
    }

    ERR("Done analyzing breadcrumbs ...\n");
    vkd3d_dbg_flush();
    pthread_mutex_unlock(&global_report_lock);
}

void vkd3d_breadcrumb_tracer_begin_command_list(struct d3d12_command_list *list)
{
    struct vkd3d_breadcrumb_tracer *breadcrumb_tracer = &list->device->breadcrumb_tracer;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_breadcrumb_command_list_trace_context *trace;
    unsigned int context = list->breadcrumb_context_index;
    struct vkd3d_breadcrumb_command cmd;

    if (context == UINT32_MAX)
        return;

    trace = &breadcrumb_tracer->trace_contexts[context];
    trace->counter++;

    cmd.count = trace->counter;
    cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER;
    vkd3d_breadcrumb_tracer_add_command(list, &cmd);

    if (list->device->vk_info.NV_device_diagnostic_checkpoints)
    {
        /* A checkpoint is implicitly a top and bottom marker. */
        cmd.count = trace->counter;
        cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER;
        vkd3d_breadcrumb_tracer_add_command(list, &cmd);

        VK_CALL(vkCmdSetCheckpointNV(list->cmd.vk_command_buffer, NV_ENCODE_CHECKPOINT(context, trace->counter)));
    }
    else if (list->device->vk_info.AMD_buffer_marker)
    {
        VK_CALL(vkCmdWriteBufferMarkerAMD(list->cmd.vk_command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, begin_marker),
                trace->counter));
    }
}

void vkd3d_breadcrumb_tracer_add_command(struct d3d12_command_list *list,
        const struct vkd3d_breadcrumb_command *command)
{
    struct vkd3d_breadcrumb_tracer *breadcrumb_tracer = &list->device->breadcrumb_tracer;
    struct vkd3d_breadcrumb_command_list_trace_context *trace;
    unsigned int context = list->breadcrumb_context_index;

    if (context == UINT32_MAX)
        return;

    trace = &breadcrumb_tracer->trace_contexts[context];

    TRACE("Adding command (%s) to context %u.\n",
            vkd3d_breadcrumb_command_type_to_str(command->type), context);

    vkd3d_array_reserve((void**)&trace->commands, &trace->command_size,
            trace->command_count + 1, sizeof(*trace->commands));
    trace->commands[trace->command_count++] = *command;
}

void vkd3d_breadcrumb_tracer_signal(struct d3d12_command_list *list)
{
    struct vkd3d_breadcrumb_tracer *breadcrumb_tracer = &list->device->breadcrumb_tracer;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_breadcrumb_command_list_trace_context *trace;
    unsigned int context = list->breadcrumb_context_index;
    struct vkd3d_breadcrumb_command cmd;

    if (context == UINT32_MAX)
        return;

    trace = &breadcrumb_tracer->trace_contexts[context];

    if (list->device->vk_info.NV_device_diagnostic_checkpoints)
    {
        trace->counter++;

        cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER;
        cmd.count = trace->counter;
        vkd3d_breadcrumb_tracer_add_command(list, &cmd);
        TRACE("Breadcrumb signal top-of-pipe context %u -> %u\n", context, cmd.count);

        cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER;
        cmd.count = trace->counter;
        vkd3d_breadcrumb_tracer_add_command(list, &cmd);
        TRACE("Breadcrumb signal bottom-of-pipe context %u -> %u\n", context, cmd.count);

        VK_CALL(vkCmdSetCheckpointNV(list->cmd.vk_command_buffer, NV_ENCODE_CHECKPOINT(context, trace->counter)));
    }
    else if (list->device->vk_info.AMD_buffer_marker)
    {
        cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER;
        cmd.count = trace->counter;
        vkd3d_breadcrumb_tracer_add_command(list, &cmd);
        TRACE("Breadcrumb signal bottom-of-pipe context %u -> %u\n", context, cmd.count);

        VK_CALL(vkCmdWriteBufferMarkerAMD(list->cmd.vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, end_marker),
                trace->counter));

        trace->counter++;

        cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER;
        cmd.count = trace->counter;
        vkd3d_breadcrumb_tracer_add_command(list, &cmd);
        TRACE("Breadcrumb signal top-of-pipe context %u -> %u\n", context, cmd.count);

        VK_CALL(vkCmdWriteBufferMarkerAMD(list->cmd.vk_command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, begin_marker),
                trace->counter));
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS_SYNC)
    {
        VkMemoryBarrier2 vk_barrier;
        VkDependencyInfo dep_info;

        d3d12_command_list_end_current_render_pass(list, true);

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        memset(&dep_info, 0, sizeof(dep_info));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }
}

void vkd3d_breadcrumb_tracer_link_submission(struct d3d12_command_list *list,
        struct d3d12_command_list *prev, struct d3d12_command_list *next)
{
    struct vkd3d_breadcrumb_tracer *breadcrumb_tracer = &list->device->breadcrumb_tracer;
    struct vkd3d_breadcrumb_command_list_trace_context *trace;
    unsigned int context = list->breadcrumb_context_index;

    trace = &breadcrumb_tracer->trace_contexts[context];
    trace->prev = prev ? prev->breadcrumb_context_index : UINT32_MAX;
    trace->next = next ? next->breadcrumb_context_index : UINT32_MAX;
}

void vkd3d_breadcrumb_tracer_end_command_list(struct d3d12_command_list *list)
{
    struct vkd3d_breadcrumb_tracer *breadcrumb_tracer = &list->device->breadcrumb_tracer;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_breadcrumb_command_list_trace_context *trace;
    unsigned int context = list->breadcrumb_context_index;
    struct vkd3d_breadcrumb_command cmd;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    if (context == UINT32_MAX)
        return;

    trace = &breadcrumb_tracer->trace_contexts[context];
    trace->counter = UINT32_MAX;

    if (list->device->vk_info.NV_device_diagnostic_checkpoints)
    {
        VK_CALL(vkCmdSetCheckpointNV(list->cmd.vk_command_buffer, NV_ENCODE_CHECKPOINT(context, trace->counter)));
    }
    else if (list->device->vk_info.AMD_buffer_marker)
    {
        VK_CALL(vkCmdWriteBufferMarkerAMD(list->cmd.vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, begin_marker),
                trace->counter));

        VK_CALL(vkCmdWriteBufferMarkerAMD(list->cmd.vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, end_marker),
                trace->counter));
    }

    /* Pure execution barrier to avoid breadcrumbs spilling between command lists. */
    memset(&vk_barrier, 0, sizeof(vk_barrier));
    memset(&dep_info, 0, sizeof(dep_info));
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_barrier;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    cmd.count = trace->counter;
    cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER;
    vkd3d_breadcrumb_tracer_add_command(list, &cmd);
    cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER;
    vkd3d_breadcrumb_tracer_add_command(list, &cmd);
}

void vkd3d_breadcrumb_tracer_update_barrier_hashes(struct vkd3d_breadcrumb_tracer *tracer)
{
    char env[VKD3D_PATH_MAX];
    size_t new_count;
    FILE *file;

    if (vkd3d_get_env_var("VKD3D_BARRIER_HASHES", env, sizeof(env)))
    {
        file = fopen(env, "r");
        if (file)
        {
            pthread_mutex_lock(&tracer->barrier_hash_lock);
            vkd3d_shader_hash_range_parse(file, &tracer->barrier_hashes, &tracer->barrier_hashes_size,
                    &new_count, VKD3D_SHADER_HASH_RANGE_KIND_BARRIERS);
            vkd3d_atomic_uint32_store_explicit(&tracer->barrier_hashes_count, new_count, vkd3d_memory_order_relaxed);
            pthread_mutex_unlock(&tracer->barrier_hash_lock);
            fclose(file);
        }
        else
            vkd3d_atomic_uint32_store_explicit(&tracer->barrier_hashes_count, 0, vkd3d_memory_order_relaxed);
    }
}

uint32_t vkd3d_breadcrumb_tracer_shader_hash_forces_barrier(struct vkd3d_breadcrumb_tracer *tracer,
        vkd3d_shader_hash_t hash)
{
    uint32_t flags = 0;
    size_t i;

    /* Avoid taking lock every dispatch when we're not explicitly using the feature.
     * Ordering is not relevant, since if we decide to look at hashes, we take full locks anyway. */
    if (vkd3d_atomic_uint32_load_explicit(&tracer->barrier_hashes_count, vkd3d_memory_order_relaxed) != 0)
    {
        pthread_mutex_lock(&tracer->barrier_hash_lock);
        for (i = 0; i < tracer->barrier_hashes_count; i++)
            if (tracer->barrier_hashes[i].lo <= hash && hash <= tracer->barrier_hashes[i].hi)
                flags |= tracer->barrier_hashes[i].flags;
        pthread_mutex_unlock(&tracer->barrier_hash_lock);
    }
    return flags;
}
