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

/* Just allocate everything up front. This only consumes host memory anyways. */
#define MAX_COMMAND_LISTS (32 * 1024)

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
        case VKD3D_BREADCRUMB_COMMAND_COPY:
            return "copy";
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
        case VKD3D_BREADCRUMB_COMMAND_COPY_RTAS:
            return "copy_rtas";
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
        case VKD3D_BREADCRUMB_COMMAND_ROOT_DESC:
            return "root_desc";
        case VKD3D_BREADCRUMB_COMMAND_ROOT_CONST:
            return "root_const";

        default:
            return "?";
    }
}

HRESULT vkd3d_breadcrumb_tracer_init(struct vkd3d_breadcrumb_tracer *tracer, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC1 resource_desc;
    HRESULT hr;
    int rc;

    memset(tracer, 0, sizeof(*tracer));

    if ((rc = pthread_mutex_init(&tracer->lock, NULL)))
        return hresult_from_errno(rc);

    if (device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory &&
            device->vk_info.AMD_buffer_marker)
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
                &resource_desc, &tracer->host_buffer)))
        {
            goto err;
        }

        /* If device faults in the middle of execution we will never get the chance to flush device caches.
         * Make sure that breadcrumbs are always written directly out.
         * This is the primary usecase for the device coherent/uncached extension after all ... */
        if (FAILED(hr = vkd3d_allocate_buffer_memory(device, tracer->host_buffer,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                        VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                        VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
                &tracer->host_buffer_memory)))
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
        /* TODO: NV checkpoints path. */
        return E_NOTIMPL;
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

    if (device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory &&
            device->vk_info.AMD_buffer_marker)
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

static void vkd3d_breadcrumb_tracer_report_command_list(
        const struct vkd3d_breadcrumb_command_list_trace_context *context,
        uint32_t begin_marker,
        uint32_t end_marker)
{
    const struct vkd3d_breadcrumb_command *cmd;
    bool observed_begin_cmd = false;
    bool observed_end_cmd = false;
    unsigned int i;

    if (end_marker == 0)
    {
        ERR(" ===== Potential crash region BEGIN (make sure RADV_DEBUG=syncshaders is used for maximum accuracy) =====\n");
        observed_begin_cmd = true;
    }

    /* We can assume that possible culprit commands lie between the end_marker
     * and top_marker. */
    for (i = 0; i < context->command_count; i++)
    {
        cmd = &context->commands[i];

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

static void vkd3d_breadcrumb_tracer_report_command_list_amd(struct vkd3d_breadcrumb_tracer *tracer,
        unsigned int context_index)
{
    const struct vkd3d_breadcrumb_command_list_trace_context *context;
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
    vkd3d_breadcrumb_tracer_report_command_list(context, begin_marker, end_marker);
    ERR("Done analyzing command list.\n");
}

void vkd3d_breadcrumb_tracer_report_device_lost(struct vkd3d_breadcrumb_tracer *tracer,
        struct d3d12_device *device)
{
    unsigned int i;

    ERR("Device lost observed, analyzing breadcrumbs ...\n");

    if (device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory &&
            device->vk_info.AMD_buffer_marker)
    {
        /* AMD path, buffer marker. */
        for (i = 0; i < MAX_COMMAND_LISTS; i++)
            vkd3d_breadcrumb_tracer_report_command_list_amd(tracer, i);
    }
    else
    {
        /* TODO: NV one is more direct, look at checkpoints and deduce it from there. */
    }

    ERR("Done analyzing breadcrumbs ...\n");
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

    if (list->device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory &&
            list->device->vk_info.AMD_buffer_marker)
    {
        VK_CALL(vkCmdWriteBufferMarkerAMD(list->vk_command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, begin_marker),
                trace->counter));
    }
    else
    {
        /* NV checkpoint */
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

    cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER;
    cmd.count = trace->counter;
    vkd3d_breadcrumb_tracer_add_command(list, &cmd);

    TRACE("Breadcrumb signal bottom-of-pipe context %u -> %u\n", context, cmd.count);

    if (list->device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory &&
            list->device->vk_info.AMD_buffer_marker)
    {
        VK_CALL(vkCmdWriteBufferMarkerAMD(list->vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, end_marker),
                trace->counter));

        VK_CALL(vkCmdWriteBufferMarkerAMD(list->vk_command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, begin_marker),
                trace->counter + 1));
    }

    trace->counter++;

    cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER;
    cmd.count = trace->counter;
    vkd3d_breadcrumb_tracer_add_command(list, &cmd);
    TRACE("Breadcrumb signal top-of-pipe context %u -> %u\n", context, cmd.count);
}

void vkd3d_breadcrumb_tracer_end_command_list(struct d3d12_command_list *list)
{
    struct vkd3d_breadcrumb_tracer *breadcrumb_tracer = &list->device->breadcrumb_tracer;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_breadcrumb_command_list_trace_context *trace;
    unsigned int context = list->breadcrumb_context_index;
    struct vkd3d_breadcrumb_command cmd;

    if (context == UINT32_MAX)
        return;

    trace = &breadcrumb_tracer->trace_contexts[context];
    trace->counter = UINT32_MAX;

    if (list->device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory &&
            list->device->vk_info.AMD_buffer_marker)
    {
        VK_CALL(vkCmdWriteBufferMarkerAMD(list->vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, begin_marker),
                trace->counter));

        VK_CALL(vkCmdWriteBufferMarkerAMD(list->vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                breadcrumb_tracer->host_buffer,
                context * sizeof(struct vkd3d_breadcrumb_counter) +
                        offsetof(struct vkd3d_breadcrumb_counter, end_marker),
                trace->counter));
    }

    cmd.count = trace->counter;
    cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER;
    vkd3d_breadcrumb_tracer_add_command(list, &cmd);
    cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER;
    vkd3d_breadcrumb_tracer_add_command(list, &cmd);
}
