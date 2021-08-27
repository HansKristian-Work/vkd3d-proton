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

static inline struct d3d12_command_list *d3d12_command_list_from_ID3D12GraphicsCommandListExt(ID3D12GraphicsCommandListExt *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandListExt_iface);
}

extern ULONG STDMETHODCALLTYPE d3d12_command_list_AddRef(d3d12_command_list_iface *iface);

ULONG STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_AddRef(ID3D12GraphicsCommandListExt *iface)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    return d3d12_command_list_AddRef(&command_list->ID3D12GraphicsCommandList_iface);
}

extern ULONG STDMETHODCALLTYPE d3d12_command_list_Release(d3d12_command_list_iface *iface);

static ULONG STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_Release(ID3D12GraphicsCommandListExt *iface)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    return d3d12_command_list_Release(&command_list->ID3D12GraphicsCommandList_iface);
}

extern HRESULT STDMETHODCALLTYPE d3d12_command_list_QueryInterface(d3d12_command_list_iface *iface,
        REFIID iid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_QueryInterface(ID3D12GraphicsCommandListExt *iface,
        REFIID iid, void **out)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_command_list_QueryInterface(&command_list->ID3D12GraphicsCommandList_iface, iid, out);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_GetVulkanHandle(ID3D12GraphicsCommandListExt *iface,
        VkCommandBuffer *pVkCommandBuffer)
{
    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    TRACE("iface %p, pVkCommandBuffer %p.\n", iface, pVkCommandBuffer);
    if (!pVkCommandBuffer)
        return E_INVALIDARG;

    *pVkCommandBuffer = command_list->vk_command_buffer;
    return S_OK;
}

#define CU_LAUNCH_PARAM_BUFFER_POINTER (const void*)0x01
#define CU_LAUNCH_PARAM_BUFFER_SIZE    (const void*)0x02
#define CU_LAUNCH_PARAM_END            (const void*)0x00

static HRESULT STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_LaunchCubinShader(ID3D12GraphicsCommandListExt *iface, D3D12_CUBIN_DATA_HANDLE *handle, UINT32 block_x, UINT32 block_y, UINT32 block_z, const void *params, UINT32 param_size)
{
    VkCuLaunchInfoNVX launchInfo = { VK_STRUCTURE_TYPE_CU_LAUNCH_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;

    const void *config[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER, params,
        CU_LAUNCH_PARAM_BUFFER_SIZE,    &param_size,
        CU_LAUNCH_PARAM_END
    };

    struct d3d12_command_list *command_list = d3d12_command_list_from_ID3D12GraphicsCommandListExt(iface);
    TRACE("iface %p, handle %p, block_x %u,  block_y %u, block_z %u, params %p, param_size %u \n", iface, handle, block_x, block_y, block_z, params, param_size);
    if (!handle || !block_x || !block_y || !block_z || !params || !param_size)
        return E_INVALIDARG;

    launchInfo.function = handle->vkCuFunction;
    launchInfo.gridDimX = block_x;
    launchInfo.gridDimY = block_y;
    launchInfo.gridDimZ = block_z;
    launchInfo.blockDimX = handle->blockX;
    launchInfo.blockDimY = handle->blockY;
    launchInfo.blockDimZ = handle->blockZ;
    launchInfo.sharedMemBytes = 0;
    launchInfo.paramCount = 0;
    launchInfo.pParams = NULL;
    launchInfo.extraCount = 1;
    launchInfo.pExtras = config;
    
    vk_procs = &command_list->device->vk_procs;
    VK_CALL(vkCmdCuLaunchKernelNVX(command_list->vk_command_buffer, &launchInfo));
    return S_OK;
}

CONST_VTBL struct ID3D12GraphicsCommandListExtVtbl d3d12_command_list_vkd3d_ext_vtbl =
{
    /* IUnknown methods */
    d3d12_command_list_vkd3d_ext_QueryInterface,
    d3d12_command_list_vkd3d_ext_AddRef,
    d3d12_command_list_vkd3d_ext_Release,

    /* ID3D12GraphicsCommandListExt methods */
    d3d12_command_list_vkd3d_ext_GetVulkanHandle,
    d3d12_command_list_vkd3d_ext_LaunchCubinShader
};

