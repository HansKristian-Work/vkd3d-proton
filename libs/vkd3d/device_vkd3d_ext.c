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

static inline struct d3d12_device *d3d12_device_from_ID3D12DeviceExt(ID3D12DeviceExt *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12DeviceExt_iface);
}

ULONG STDMETHODCALLTYPE d3d12_device_vkd3d_ext_AddRef(ID3D12DeviceExt *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    return d3d12_device_add_ref(device);
}

static ULONG STDMETHODCALLTYPE d3d12_device_vkd3d_ext_Release(ID3D12DeviceExt *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    return d3d12_device_release(device);
}

extern HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
        REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_QueryInterface(ID3D12DeviceExt *iface,
        REFIID iid, void **out)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_device_QueryInterface(&device->ID3D12Device_iface, iid, out);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetVulkanHandles(ID3D12DeviceExt *iface, VkInstance *vk_instance, VkPhysicalDevice *vk_physical_device, VkDevice *vk_device)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    TRACE("iface %p, vk_instance %p, vk_physical_device %u, vk_device %p \n", iface, vk_instance, vk_physical_device, vk_device);
    if (!vk_device || !vk_instance || !vk_physical_device)
        return E_INVALIDARG;
        
    *vk_instance = device->vkd3d_instance->vk_instance;
    *vk_physical_device = device->vk_physical_device;
    *vk_device = device->vk_device;
    return S_OK;
}

static BOOL STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetExtensionSupport(ID3D12DeviceExt *iface, D3D12_VK_EXTENSION extension)
{
    const struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    bool ret_val = false;
    
    TRACE("iface %p, extension %u \n", iface, extension);
    switch (extension)
    {
        case D3D12_VK_NVX_BINARY_IMPORT:
            ret_val = device->vk_info.NVX_binary_import;
            break;
        case D3D12_VK_NVX_IMAGE_VIEW_HANDLE:
            ret_val = device->vk_info.NVX_image_view_handle;
            break;
        default:
            WARN("Invalid extension %x\n", extension);
    }
    
    return ret_val;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_CreateCubinComputeShaderWithName(ID3D12DeviceExt *iface, const void *cubin_data,
       UINT32 cubin_size, UINT32 block_x, UINT32 block_y, UINT32 block_z, const char *shader_name, D3D12_CUBIN_DATA_HANDLE **out_handle)
{
    VkCuFunctionCreateInfoNVX functionCreateInfo = { VK_STRUCTURE_TYPE_CU_FUNCTION_CREATE_INFO_NVX };
    VkCuModuleCreateInfoNVX moduleCreateInfo = { VK_STRUCTURE_TYPE_CU_MODULE_CREATE_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;
    D3D12_CUBIN_DATA_HANDLE *handle;
    struct d3d12_device *device;
    VkDevice vk_device;
    VkResult vr;
    
    TRACE("iface %p, cubin_data %p, cubin_size %u, shader_name %s \n", iface, cubin_data, cubin_size, shader_name);
    if (!cubin_data || !cubin_size || !shader_name)
        return E_INVALIDARG;    

    device = d3d12_device_from_ID3D12DeviceExt(iface);
    vk_device = device->vk_device;
    handle = vkd3d_calloc(1, sizeof(D3D12_CUBIN_DATA_HANDLE));
    handle->blockX = block_x;
    handle->blockY = block_y;
    handle->blockZ = block_z;

    moduleCreateInfo.pData = cubin_data;
    moduleCreateInfo.dataSize = cubin_size;
    vk_procs = &device->vk_procs;
    if ((vr = VK_CALL(vkCreateCuModuleNVX(vk_device, &moduleCreateInfo, NULL, &handle->vkCuModule))) < 0)
    {
        ERR("Failed to create cubin shader, vr %d.\n", vr);
        vkd3d_free(handle);
        return hresult_from_vk_result(vr);
    }

    functionCreateInfo.module = handle->vkCuModule;
    functionCreateInfo.pName = shader_name;

    if ((vr = VK_CALL(vkCreateCuFunctionNVX(vk_device, &functionCreateInfo, NULL, &handle->vkCuFunction))) < 0)
    {
        ERR("Failed to create cubin function module, vr %d.\n", vr);
        VK_CALL(vkDestroyCuModuleNVX(vk_device, handle->vkCuModule, NULL));
        vkd3d_free(handle);
        return hresult_from_vk_result(vr);
    }
    
    *out_handle = handle;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_DestroyCubinComputeShader(ID3D12DeviceExt *iface, D3D12_CUBIN_DATA_HANDLE *handle)
{   
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_device *device;
    VkDevice vk_device;

    TRACE("iface %p, handle %p \n", iface, handle);
    if (!iface || !handle)
        return E_INVALIDARG;
    
    device = d3d12_device_from_ID3D12DeviceExt(iface);
    vk_device = device->vk_device;
    vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyCuFunctionNVX(vk_device, handle->vkCuFunction, NULL));
    VK_CALL(vkDestroyCuModuleNVX(vk_device, handle->vkCuModule, NULL));
    vkd3d_free(handle);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetCudaTextureObject(ID3D12DeviceExt *iface, D3D12_CPU_DESCRIPTOR_HANDLE srv_handle,
       D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle, UINT32 *cuda_texture_handle)
{
    VkImageViewHandleInfoNVX imageViewHandleInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_desc *sampler_desc;
    struct d3d12_device *device;
    struct d3d12_desc *srv_desc;
    
    TRACE("iface %p, srv_handle %#x, sampler_handle %#x, cuda_texture_handle %p.\n", iface, srv_handle, sampler_handle, cuda_texture_handle);
    if (!cuda_texture_handle)
       return E_INVALIDARG;

    device = d3d12_device_from_ID3D12DeviceExt(iface);
    srv_desc = d3d12_desc_from_cpu_handle(srv_handle);
    sampler_desc = d3d12_desc_from_cpu_handle(sampler_handle);

    imageViewHandleInfo.imageView = srv_desc->info.view->vk_image_view;
    imageViewHandleInfo.sampler = sampler_desc->info.view->vk_sampler;
    imageViewHandleInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    vk_procs = &device->vk_procs;
    *cuda_texture_handle = VK_CALL(vkGetImageViewHandleNVX(device->vk_device, &imageViewHandleInfo));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetCudaSurfaceObject(ID3D12DeviceExt *iface, D3D12_CPU_DESCRIPTOR_HANDLE uav_handle, 
        UINT32 *cuda_surface_handle)
{
    VkImageViewHandleInfoNVX imageViewHandleInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_device *device;
    struct d3d12_desc *uav_desc;
    
    TRACE("iface %p, uav_handle %#x, cuda_surface_handle %p.\n", iface, uav_handle, cuda_surface_handle);
    if (!cuda_surface_handle)
       return E_INVALIDARG;

    device = d3d12_device_from_ID3D12DeviceExt(iface);
    uav_desc = d3d12_desc_from_cpu_handle(uav_handle);

    imageViewHandleInfo.imageView = uav_desc->info.view->vk_image_view;
    imageViewHandleInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    vk_procs = &device->vk_procs;
    *cuda_surface_handle = VK_CALL(vkGetImageViewHandleNVX(device->vk_device, &imageViewHandleInfo));
    return S_OK; 
}

extern __thread struct D3D12_UAV_INFO *d3d12_uav_info;

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_CaptureUAVInfo(ID3D12DeviceExt *iface, D3D12_UAV_INFO *uav_info)
{
    if (!uav_info)
       return E_INVALIDARG;

    TRACE("iface %p, uav_info %p.\n", iface, uav_info);
    
    /* CaptureUAVInfo() supposed to capture the information from the next CreateUnorderedAccess() on the same thread. 
       We use d3d12_uav_info pointer to update the information in CreateUnorderedAccess() */
    d3d12_uav_info = uav_info;
    return S_OK;
}

CONST_VTBL struct ID3D12DeviceExtVtbl d3d12_device_vkd3d_ext_vtbl =
{
    /* IUnknown methods */
    d3d12_device_vkd3d_ext_QueryInterface,
    d3d12_device_vkd3d_ext_AddRef,
    d3d12_device_vkd3d_ext_Release,

    /* ID3D12DeviceExt methods */
    d3d12_device_vkd3d_ext_GetVulkanHandles,
    d3d12_device_vkd3d_ext_GetExtensionSupport,
    d3d12_device_vkd3d_ext_CreateCubinComputeShaderWithName,
    d3d12_device_vkd3d_ext_DestroyCubinComputeShader,
    d3d12_device_vkd3d_ext_GetCudaTextureObject,
    d3d12_device_vkd3d_ext_GetCudaSurfaceObject,
    d3d12_device_vkd3d_ext_CaptureUAVInfo
};

