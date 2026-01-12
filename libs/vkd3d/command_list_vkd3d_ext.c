/*
 * * Copyright 2021 NVIDIA Corporation
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

static inline struct d3d12_command_list *d3d12_command_list_from_ID3D12GraphicsCommandListExt(d3d12_command_list_vkd3d_ext_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandListExt_iface);
}

extern ULONG STDMETHODCALLTYPE d3d12_command_list_AddRef(d3d12_command_list_iface *iface);

ULONG STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_AddRef(d3d12_command_list_vkd3d_ext_iface *iface)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    return d3d12_command_list_AddRef(&command_list->ID3D12GraphicsCommandList_iface);
}

extern ULONG STDMETHODCALLTYPE d3d12_command_list_Release(d3d12_command_list_iface *iface);

static ULONG STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_Release(d3d12_command_list_vkd3d_ext_iface *iface)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    return d3d12_command_list_Release(&command_list->ID3D12GraphicsCommandList_iface);
}

extern HRESULT STDMETHODCALLTYPE d3d12_command_list_QueryInterface(d3d12_command_list_iface *iface,
        REFIID iid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_QueryInterface(d3d12_command_list_vkd3d_ext_iface *iface,
        REFIID iid, void **out)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_command_list_QueryInterface(&command_list->ID3D12GraphicsCommandList_iface, iid, out);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_GetVulkanHandle(d3d12_command_list_vkd3d_ext_iface *iface,
        VkCommandBuffer *pVkCommandBuffer)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    TRACE("iface %p, pVkCommandBuffer %p.\n", iface, pVkCommandBuffer);
    if (!pVkCommandBuffer)
        return E_INVALIDARG;

    *pVkCommandBuffer = command_list->cmd.vk_command_buffer;
    /* TODO: Do we need to block any attempt to split command buffers here?
     * Might be a problem if DLSS implementation caches the VkCommandBuffer across DLSS invocations. */
    return S_OK;
}

#define CU_LAUNCH_PARAM_BUFFER_POINTER (const void*)0x01
#define CU_LAUNCH_PARAM_BUFFER_SIZE    (const void*)0x02
#define CU_LAUNCH_PARAM_END            (const void*)0x00

static HRESULT STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_LaunchCubinShaderEx(d3d12_command_list_vkd3d_ext_iface *iface, D3D12_CUBIN_DATA_HANDLE *handle, UINT32 block_x, UINT32 block_y, UINT32 block_z, UINT32 smem_size, const void *params, UINT32 param_size, const void *raw_params, UINT32 raw_params_count)
{
    VkCuLaunchInfoNVX launchInfo = { VK_STRUCTURE_TYPE_CU_LAUNCH_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;

    const void *config[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER, params,
        CU_LAUNCH_PARAM_BUFFER_SIZE,    &param_size,
        CU_LAUNCH_PARAM_END
    };

    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    TRACE("iface %p, handle %p, block_x %u, block_y %u, block_z %u, smem_size %u, params %p, param_size %u, raw_params %p, raw_params_count %u\n",
           iface, handle, block_x, block_y, block_z, smem_size, params, param_size, raw_params, raw_params_count);

    if (!handle || !block_x || !block_y || !block_z || !params || !param_size)
        return E_INVALIDARG;

    launchInfo.function = handle->vkCuFunction;
    launchInfo.gridDimX = block_x;
    launchInfo.gridDimY = block_y;
    launchInfo.gridDimZ = block_z;
    launchInfo.blockDimX = handle->blockX;
    launchInfo.blockDimY = handle->blockY;
    launchInfo.blockDimZ = handle->blockZ;
    launchInfo.sharedMemBytes = smem_size;
    launchInfo.paramCount = raw_params_count;
    launchInfo.pParams = raw_params;
    launchInfo.extraCount = 1;
    launchInfo.pExtras = config;
    
    vk_procs = &command_list->device->vk_procs;
    VK_CALL(vkCmdCuLaunchKernelNVX(command_list->cmd.vk_command_buffer, &launchInfo));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_LaunchCubinShader(d3d12_command_list_vkd3d_ext_iface *iface, D3D12_CUBIN_DATA_HANDLE *handle, UINT32 block_x, UINT32 block_y, UINT32 block_z, const void *params, UINT32 param_size)
{
    return d3d12_command_list_vkd3d_ext_LaunchCubinShaderEx(iface,
                                                            handle,
                                                            block_x,
                                                            block_y,
                                                            block_z,
                                                            0, /* smem_size */
                                                            params,
                                                            param_size,
                                                            NULL, /* raw_params */
                                                            0 /* raw_params_count */);
}

static void STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_InputAttachmentPixelBarrier(
        ID3D12GraphicsCommandListExt2 *iface)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &command_list->device->vk_procs;
    VkMemoryBarrier2 vk_memory_barrier;
    VkDependencyInfo dep;

    TRACE("iface %p\n", iface);

    memset(&dep, 0, sizeof(dep));
    memset(&vk_memory_barrier, 0, sizeof(vk_memory_barrier));

    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &vk_memory_barrier;

    vk_memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;

    /* Add fragment shader to srcStage to add WAR hazard. */
    vk_memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR |
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
    vk_memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;

    vk_memory_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vk_memory_barrier.dstAccessMask = VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;

    VK_CALL(vkCmdPipelineBarrier2(command_list->cmd.vk_command_buffer, &dep));
}

static void STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_SetRootSignatureInputAttachments(
        ID3D12GraphicsCommandListExt2 *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    VkDeviceSize offset;
    uint32_t info_index;

    TRACE("iface %p, handle %016"PRIx64"\n", iface, handle.ptr);

    if (!command_list->device->tiler_optimizations.enable)
    {
        FIXME("Tiler optimizations are not enabled on device.\n");
        return;
    }

    if (d3d12_device_use_embedded_mutable_descriptors(command_list->device))
    {
        offset = d3d12_desc_heap_offset_from_embedded_gpu_handle(
                handle, command_list->device->bindless_state.descriptor_buffer_cbv_srv_uav_size_log2,
                command_list->device->bindless_state.descriptor_buffer_sampler_size_log2);

        /* No need to search for the set info since it's static for embedded mutable. */
        assert(command_list->device->bindless_state.set_info[1].heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        assert(command_list->device->bindless_state.set_info[1].flags & VKD3D_BINDLESS_SET_IMAGE);
        info_index = 1;
    }
    else
    {
        offset = d3d12_desc_heap_offset_from_gpu_handle(handle);

        info_index = vkd3d_bindless_state_find_set_info_index_fast(command_list->device,
            VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_TYPED,
            VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_IMAGE);
    }

    offset *= command_list->device->bindless_state.set_info[info_index].host_mapping_descriptor_size;
    offset += command_list->device->bindless_state.set_info[info_index].host_mapping_offset;

    command_list->graphics_bindings.input_attachment_desc_buffer_offset = offset;
    command_list->graphics_bindings.dirty_flags |= VKD3D_PIPELINE_DIRTY_INPUT_ATTACHMENT_SET;
}

static void STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_SetInputAttachmentFeedback(
        ID3D12GraphicsCommandListExt2 *iface,
        UINT render_target_concurrent_mask,
        BOOL depth_concurrent, BOOL stencil_concurrent)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    unsigned int i;

    TRACE("iface %p, render_target_concurrent_mask #%x, depth_concurrent %u, stencil_concurrent %u\n",
        iface, render_target_concurrent_mask, depth_concurrent, stencil_concurrent);

    /* Pre maintenance10, drivers just have to deal with it somehow. */
    if (!command_list->device->device_info.maintenance10_features.maintenance10)
        return;

    d3d12_command_list_end_current_render_pass(command_list, false);

    for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    {
        if (render_target_concurrent_mask & (1u << i))
        {
            command_list->rendering_info.flags_info[i].flags |=
                VK_RENDERING_ATTACHMENT_INPUT_ATTACHMENT_FEEDBACK_BIT_KHR;
        }
        else
        {
            command_list->rendering_info.flags_info[i].flags &=
                ~VK_RENDERING_ATTACHMENT_INPUT_ATTACHMENT_FEEDBACK_BIT_KHR;
        }
    }

    if (depth_concurrent)
    {
        command_list->rendering_info.flags_info[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 0].flags |=
            VK_RENDERING_ATTACHMENT_INPUT_ATTACHMENT_FEEDBACK_BIT_KHR;
    }
    else
    {
        command_list->rendering_info.flags_info[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 0].flags &=
            VK_RENDERING_ATTACHMENT_INPUT_ATTACHMENT_FEEDBACK_BIT_KHR;
    }

    if (stencil_concurrent)
    {
        command_list->rendering_info.flags_info[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1].flags |=
            VK_RENDERING_ATTACHMENT_INPUT_ATTACHMENT_FEEDBACK_BIT_KHR;
    }
    else
    {
        command_list->rendering_info.flags_info[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1].flags &=
            VK_RENDERING_ATTACHMENT_INPUT_ATTACHMENT_FEEDBACK_BIT_KHR;
    }
}

CONST_VTBL struct ID3D12GraphicsCommandListExt2Vtbl d3d12_command_list_vkd3d_ext_vtbl =
{
    /* IUnknown methods */
    d3d12_command_list_vkd3d_ext_QueryInterface,
    d3d12_command_list_vkd3d_ext_AddRef,
    d3d12_command_list_vkd3d_ext_Release,

    /* ID3D12GraphicsCommandListExt methods */
    d3d12_command_list_vkd3d_ext_GetVulkanHandle,
    d3d12_command_list_vkd3d_ext_LaunchCubinShader,

    /* ID3D12GraphicsCommandListExt1 methods */
    d3d12_command_list_vkd3d_ext_LaunchCubinShaderEx,

    /* ID3D12GraphicsCommandListExt2 methods */
    d3d12_command_list_vkd3d_ext_InputAttachmentPixelBarrier,
    d3d12_command_list_vkd3d_ext_SetRootSignatureInputAttachments,
    d3d12_command_list_vkd3d_ext_SetInputAttachmentFeedback,
};

