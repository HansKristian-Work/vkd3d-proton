/*
 * Copyright 2023 Philip Rebohle for Valve Corporation
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

/* Determined by calling EnumerateMetaCommands on Windows drivers */
const GUID IID_META_COMMAND_DSTORAGE = {0x1bddd090,0xc47e,0x459c,{0x8f,0x81,0x42,0xc9,0xf9,0x7a,0x53,0x08}};

static HRESULT d3d12_meta_command_create_dstorage(struct d3d12_meta_command *meta_command,
        struct d3d12_device *device, const void *parameter_data, size_t parameter_size);

struct d3d12_meta_command_dstorage_create_args
{
    UINT64 version;
    UINT64 format;
    UINT64 max_streams;
    UINT64 flags;
};

struct d3d12_meta_command_dstorage_exec_args
{
    D3D12_GPU_VIRTUAL_ADDRESS input_buffer_va;
    UINT64 input_buffer_size;
    D3D12_GPU_VIRTUAL_ADDRESS output_buffer_va;
    UINT64 output_buffer_size;
    D3D12_GPU_VIRTUAL_ADDRESS control_buffer_va;
    UINT64 control_buffer_size;
    D3D12_GPU_VIRTUAL_ADDRESS scratch_buffer_va;
    UINT64 scratch_buffer_size;
    UINT64 stream_count;
    D3D12_GPU_VIRTUAL_ADDRESS status_buffer_va;
    UINT64 status_buffer_size;
};

struct d3d12_meta_command_parameter_info
{
    D3D12_META_COMMAND_PARAMETER_STAGE stage;
    D3D12_META_COMMAND_PARAMETER_DESC desc;
};

struct d3d12_meta_command_info
{
    REFGUID command_id;
    LPCWSTR name;
    D3D12_GRAPHICS_STATES init_dirty_states;
    D3D12_GRAPHICS_STATES exec_dirty_states;
    unsigned int parameter_count;
    const struct d3d12_meta_command_parameter_info *parameters;
    d3d12_meta_command_create_proc create_proc;
};

/* Determined by calling EnumerateMetaCommandParameters on Windows drivers */
static const struct d3d12_meta_command_parameter_info d3d12_meta_command_dstorage_parameter_infos[] =
{
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"Version", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, version) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"Format", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, format) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"MaxStreams", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, max_streams) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"Flags", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, flags) } },

    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"InputBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, input_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"InputBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, input_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"OutputBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_OUTPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, output_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"OutputBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, output_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ControlBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, control_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ControlBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, control_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ScratchBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_OUTPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, scratch_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ScratchBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, scratch_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"StreamCount", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, stream_count) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"StatusBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_OUTPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, status_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"StatusBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, status_buffer_size) } },
};

const struct d3d12_meta_command_info d3d12_meta_command_infos[] =
{
    { &IID_META_COMMAND_DSTORAGE, u"DirectStorage", 0,
          D3D12_GRAPHICS_STATE_COMPUTE_ROOT_SIGNATURE | D3D12_GRAPHICS_STATE_PIPELINE_STATE,
          ARRAY_SIZE(d3d12_meta_command_dstorage_parameter_infos),
          d3d12_meta_command_dstorage_parameter_infos, &d3d12_meta_command_create_dstorage },
};

static const struct d3d12_meta_command_info *vkd3d_get_meta_command_info(REFGUID guid)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(d3d12_meta_command_infos); i++)
    {
        if (!memcmp(guid, d3d12_meta_command_infos[i].command_id, sizeof(*guid)))
            return &d3d12_meta_command_infos[i];
    }

    return NULL;
}

static bool vkd3d_check_meta_command_support(struct d3d12_device *device, REFGUID command_id)
{
    if (!memcmp(command_id, &IID_META_COMMAND_DSTORAGE, sizeof(*command_id)))
    {
        if (!device->meta_ops.dstorage.vk_emit_nv_memory_decompression_regions_pipeline)
            return false;

        return d3d12_device_use_nv_memory_decompression(device) ||
                device->meta_ops.dstorage.vk_gdeflate_pipeline;
    }

    return false;
}

void vkd3d_enumerate_meta_commands(struct d3d12_device *device, UINT *count, D3D12_META_COMMAND_DESC *output_descs)
{
    uint32_t max_count, out_count;
    unsigned int i;

    max_count = output_descs ? *count : 0;
    out_count = 0;

    for (i = 0; i < ARRAY_SIZE(d3d12_meta_command_infos); i++)
    {
        const struct d3d12_meta_command_info *info = &d3d12_meta_command_infos[i];

        if (!vkd3d_check_meta_command_support(device, info->command_id))
            continue;

        if (out_count < max_count)
        {
            D3D12_META_COMMAND_DESC *output_desc = &output_descs[out_count];
            output_desc->Id = *info->command_id;
            output_desc->Name = info->name;
            output_desc->InitializationDirtyState = info->init_dirty_states;
            output_desc->ExecutionDirtyState = info->exec_dirty_states;
        }

        out_count++;
    }

    *count = out_count;
}

bool vkd3d_enumerate_meta_command_parameters(struct d3d12_device *device, REFGUID command_id,
        D3D12_META_COMMAND_PARAMETER_STAGE stage, UINT *total_size, UINT *param_count,
        D3D12_META_COMMAND_PARAMETER_DESC *param_descs)
{
    const struct d3d12_meta_command_info *command_info = vkd3d_get_meta_command_info(command_id);
    uint32_t struct_size, max_count, out_count;
    unsigned int i;

    if (!command_info)
        return false;

    if (!vkd3d_check_meta_command_support(device, command_info->command_id))
        return false;

    max_count = param_count ? *param_count : 0u;
    out_count = 0;

    struct_size = 0;

    for (i = 0; i < command_info->parameter_count; i++)
    {
        const struct d3d12_meta_command_parameter_info *param = &command_info->parameters[i];

        if (param->stage != stage)
            continue;

        if (out_count < max_count)
            param_descs[out_count] = param->desc;

        out_count += 1;
        struct_size = max(struct_size, param->desc.StructureOffset + sizeof(uint64_t));
    }

    if (param_count)
        *param_count = out_count;

    if (total_size)
        *total_size = struct_size;

    return true;
}

static void d3d12_meta_command_destroy(struct d3d12_meta_command *meta_command)
{
    d3d_destruction_notifier_free(&meta_command->destruction_notifier);
    vkd3d_private_store_destroy(&meta_command->private_store);

    vkd3d_free(meta_command);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_QueryInterface(d3d12_meta_command_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12MetaCommand)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12MetaCommand_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&meta_command->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &meta_command->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_meta_command_AddRef(d3d12_meta_command_iface *iface)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);
    ULONG refcount = InterlockedIncrement(&meta_command->refcount);

    TRACE("%p increasing refcount to %u.\n", meta_command, refcount);

    if (refcount == 1)
    {
        struct d3d12_device *device = meta_command->device;

        d3d12_device_add_ref(device);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_meta_command_Release(d3d12_meta_command_iface *iface)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);
    struct d3d12_device *device = meta_command->device;
    ULONG refcount;

    refcount = InterlockedDecrement(&meta_command->refcount);

    TRACE("%p decreasing refcount to %u.\n", meta_command, refcount);

    if (!refcount)
    {
        d3d12_device_release(device);
        d3d12_meta_command_destroy(meta_command);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_GetPrivateData(d3d12_meta_command_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&meta_command->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_SetPrivateData(d3d12_meta_command_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&meta_command->private_store, guid, data_size, data, NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_SetPrivateDataInterface(d3d12_meta_command_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&meta_command->private_store, guid, data, NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_GetDevice(d3d12_meta_command_iface *iface, REFIID iid, void **device)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(meta_command->device, iid, device);
}

static UINT64 STDMETHODCALLTYPE d3d12_meta_command_GetRequiredParameterResourceSize(d3d12_meta_command_iface *iface,
        D3D12_META_COMMAND_PARAMETER_STAGE stage, UINT parameter_index)
{
    FIXME("iface %p, stage %u, parameter_index %u, stub!\n", iface, stage, parameter_index);

    return 0;
}

CONST_VTBL struct ID3D12MetaCommandVtbl d3d12_meta_command_vtbl =
{
    /* IUnknown methods */
    d3d12_meta_command_QueryInterface,
    d3d12_meta_command_AddRef,
    d3d12_meta_command_Release,
    /* ID3D12Object methods */
    d3d12_meta_command_GetPrivateData,
    d3d12_meta_command_SetPrivateData,
    d3d12_meta_command_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_meta_command_GetDevice,
    /* ID3D12MetaCommand methods */
    d3d12_meta_command_GetRequiredParameterResourceSize,
};

static void d3d12_meta_command_exec_dstorage(struct d3d12_meta_command *meta_command,
        struct d3d12_command_list *list, const void *parameter_data, size_t parameter_size)
{
    const struct d3d12_meta_command_dstorage_exec_args *parameters = parameter_data;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_meta_ops *meta_ops = &list->device->meta_ops;
    uint32_t workgroup_data_offset, workgroup_count, scratch_offset;
    const struct vkd3d_unique_resource *scratch_buffer;
    struct vkd3d_dstorage_decompress_args push_args;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;
    unsigned int i;

    TRACE("input_va %"PRIx64", input_size %"PRIu64", output_va %"PRIx64", output_size %"PRIu64", "
            "control_va %"PRIx64", control_size %"PRIu64", scratch_va %"PRIx64", scratch_size %"PRIu64", "
            "status_va %"PRIx64", status_size %"PRIu64", stream_count %"PRIu64".\n",
            parameters->input_buffer_va, parameters->input_buffer_size,
            parameters->output_buffer_va, parameters->output_buffer_size,
            parameters->control_buffer_va, parameters->control_buffer_size,
            parameters->scratch_buffer_va, parameters->scratch_buffer_size,
            parameters->status_buffer_va, parameters->status_buffer_size,
            parameters->stream_count);

    d3d12_command_list_debug_mark_begin_region(list, "DStorage");

    scratch_buffer = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, parameters->scratch_buffer_va);
    assert(scratch_buffer);

    scratch_offset = parameters->scratch_buffer_va - scratch_buffer->va;

    /* This barrier is pure nonsense and should not be needed, but apparently it is anyway.
     * FF XVI demo has a pattern of copy -> copy-to-uav barrier -> gdeflate.
     * We're synchronized with UAV here, so this should work fine, but apparently it doesn't
     * and this arbitrary COMPUTE -> COMPUTE barrier happens to work around the issue.
     * I can only deduce this is a driver bug somehow with barrier emission. */
    memset(&vk_barrier, 0, sizeof(vk_barrier));
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    memset(&push_args, 0, sizeof(push_args));
    push_args.control_va = parameters->control_buffer_va;
    push_args.src_buffer_va = parameters->input_buffer_va;
    push_args.dst_buffer_va = parameters->output_buffer_va;
    push_args.scratch_va = parameters->scratch_buffer_va;
    push_args.stream_count = parameters->stream_count;

    if (d3d12_device_use_nv_memory_decompression(list->device))
    {
        /* Offset within the scratch buffer where we are going to store
         * workgroup counts for the memory region preprocessing step. */
        workgroup_data_offset = sizeof(struct d3d12_meta_command_dstorage_scratch_header);

        /* The first dispatch will compute the number of workgroups needed
         * to process the tiles within each stream, and also reset the tile
         * count passed to vkCmdDecompressMemoryIndirectCountNV later. */
        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                meta_ops->dstorage.vk_emit_nv_memory_decompression_workgroups_pipeline));

        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, meta_ops->dstorage.vk_dstorage_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_args), &push_args));

        workgroup_count = vkd3d_compute_workgroup_count(parameters->stream_count, 32);
        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, workgroup_count, 1, 1));

        /* Iterate over individual streams and dispatch another compute shader
         * that emits the actual VkDecompressMemoryRegionNV structures. */
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                meta_ops->dstorage.vk_emit_nv_memory_decompression_regions_pipeline));

        for (i = 0; i < parameters->stream_count; i++)
        {
            push_args.stream_index = i;

            VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, meta_ops->dstorage.vk_dstorage_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_args), &push_args));

            VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, scratch_buffer->vk_buffer,
                    scratch_offset + workgroup_data_offset + i * sizeof(VkDispatchIndirectCommand)));
        }

        /* Decompress all submitted streams in one go. */
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        VK_CALL(vkCmdDecompressMemoryIndirectCountNV(list->cmd.vk_command_buffer,
                push_args.scratch_va + offsetof(struct d3d12_meta_command_dstorage_scratch_header, regions),
                push_args.scratch_va, sizeof(VkDecompressMemoryRegionNV)));
    }
    else
    {
        /* First dispatch generates one indirect dispatch command per thread */
        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                meta_ops->dstorage.vk_gdeflate_prepare_pipeline));

        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, meta_ops->dstorage.vk_dstorage_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_args), &push_args));

        workgroup_count = vkd3d_compute_workgroup_count(parameters->stream_count, 32);
        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, workgroup_count, 1, 1));

        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        /* Dispatch decompression shader with one dispatch per input stream */
        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                meta_ops->dstorage.vk_gdeflate_pipeline));

        for (i = 0; i < parameters->stream_count; i++)
        {
            push_args.stream_index = i;

            VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, meta_ops->dstorage.vk_dstorage_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_args), &push_args));

            VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, scratch_buffer->vk_buffer,
                    scratch_offset + i * sizeof(VkDispatchIndirectCommand)));
        }
    }

    /* DirectStorage does not query expected resource states from the implementation, so
     * all buffers must end up in the equivalent of D3D12_RESOURCE_STATE_UNORDERED_ACCESS. */
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    d3d12_command_list_debug_mark_end_region(list);

    VKD3D_BREADCRUMB_TAG("[Control VA + Size, Input VA + Size, Output VA + Size, Scratch VA + Size, StreamCount]");
    VKD3D_BREADCRUMB_AUX64(parameters->control_buffer_va);
    VKD3D_BREADCRUMB_AUX64(parameters->control_buffer_size);
    VKD3D_BREADCRUMB_AUX64(parameters->input_buffer_va);
    VKD3D_BREADCRUMB_AUX64(parameters->input_buffer_size);
    VKD3D_BREADCRUMB_AUX64(parameters->output_buffer_va);
    VKD3D_BREADCRUMB_AUX64(parameters->output_buffer_size);
    VKD3D_BREADCRUMB_AUX64(parameters->scratch_buffer_va);
    VKD3D_BREADCRUMB_AUX64(parameters->scratch_buffer_size);
    VKD3D_BREADCRUMB_AUX32(parameters->stream_count);
    VKD3D_BREADCRUMB_COMMAND(DSTORAGE);
}

static HRESULT d3d12_meta_command_create_dstorage(struct d3d12_meta_command *meta_command,
        struct d3d12_device *device, const void* parameter_data, size_t parameter_size)
{
    const struct d3d12_meta_command_dstorage_create_args *parameters = parameter_data;

    if (parameter_size < sizeof(*parameters) || !parameters)
    {
        FIXME("Invalid parameter set (size = %zu, ptr = %p) for DirectStorage meta command.\n", parameter_size, parameters);
        return E_INVALIDARG;
    }

    if (parameters->version != 1)
    {
        ERR("Unsupported version %"PRIu64" for DirectStorage meta command.\n", parameters->version);
        return DXGI_ERROR_UNSUPPORTED;
    }

    if (parameters->format != 1)
    {
        ERR("Unsupported format %"PRIu64" for DirectStorage meta command.\n", parameters->format);
        return DXGI_ERROR_UNSUPPORTED;
    }

    if (parameters->flags != 0)
        FIXME("Unrecognized flags %#"PRIx64" for DirectStorage meta command.\n", parameters->flags);

    meta_command->exec_proc = &d3d12_meta_command_exec_dstorage;
    return S_OK;
}

HRESULT d3d12_meta_command_create(struct d3d12_device *device, REFGUID guid,
        const void *parameters, size_t parameter_size, struct d3d12_meta_command **meta_command)
{
    const struct d3d12_meta_command_info *command_info;
    struct d3d12_meta_command *object;
    HRESULT hr;

    if (!vkd3d_check_meta_command_support(device, guid))
    {
        FIXME("Unsupported meta command %s.\n", debugstr_guid(guid));
        return E_INVALIDARG;
    }

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));
    object->ID3D12MetaCommand_iface.lpVtbl = &d3d12_meta_command_vtbl;
    object->refcount = 1;
    object->device = device;

    command_info = vkd3d_get_meta_command_info(guid);
    assert(command_info && command_info->create_proc);

    if (FAILED(hr = command_info->create_proc(object, device, parameters, parameter_size)))
    {
        vkd3d_free(object);
        return hr;
    }

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    d3d_destruction_notifier_init(&object->destruction_notifier, (IUnknown*)&object->ID3D12MetaCommand_iface);

    d3d12_device_add_ref(device);

    *meta_command = object;
    return S_OK;
}

struct d3d12_meta_command *impl_from_ID3D12MetaCommand(ID3D12MetaCommand *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_meta_command_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_meta_command, ID3D12MetaCommand_iface);
}
